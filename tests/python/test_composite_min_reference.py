"""Independent N01--N08 SelectMin reference assertions, not product adoption.

The byte-pinned Max reader supplies test-owned scalar/LUT math, raw transport
and structural host checks within their qualified scopes. Inputs and Min-entry
transitions below are independent of the capture producer and imagejxx.
"""
from __future__ import annotations

import copy
import hashlib
import os
from pathlib import Path, PurePosixPath

import pytest
import test_composite_max_reference as qualified


ROOT = Path(__file__).resolve().parents[2]
CONTRACT = "g033.composite-min-entry-reference.001"
CASE_IDS = tuple(f"N{i:02d}" for i in range(1,9))
EXPECTED_COLLECTION_COUNT = 8
EXPECTED_COLLECTION_IDS = tuple(f"test_composite_min_reference[{case}]" for case in CASE_IDS)
STAGE_COUNTS = (7,4,7,4,4,4,7,4)
RGB_COUNTS = (6,3,6,3,3,3,6,3)
DISPATCH_COUNTS = (2,1,2,1,1,1,2,1)
MAX_PAYLOAD, MAX_BUILD = 2*1024**2, 32*1024**2
STAGE = ROOT/"build/references/composite-min-imagej154r-001"
ENTRY = {"path":"docs/parity/reviews/G033-select-min-entry-001.json",
         "bytes":48102,
         "sha256":"2ca9846870223d9189c9819d4ad581d5ddcfc9055ff16162103c8a63b5f7482f"}
MAX_READER = {"path":"tests/python/test_composite_max_reference.py","bytes":45146,
              "sha256":"17954cda6ae9a0ac7bff699785961923ab4ac5b30b58d87667c4d4958c8d3e61"}
PRODUCER_SHA = "84e355ed4a509c1e95ae63f8736cddf3f23a68c6ce2bad0bbe91e0fb37e8f16a"
JAVA_CLASS = "CompositeMinReference"
SOURCE_PINS = {**qualified.SOURCE_PINS,
    "ij/macro/Functions.java":"fca5c8063c646a49f8b89ca5f4420f6cdbc00b1e28994aedec01ae0d0451c853",
    "ij/macro/Interpreter.java":"a851bee634ce4d85a450b9cd10060104e397e5b5de80e4d85344db30546c7a57"}


def _reuse_identity():
    assert Path(qualified.__file__).resolve() == ROOT/MAX_READER["path"]
    raw = qualified._pin(MAX_READER)
    assert len(raw) == MAX_READER["bytes"]
    qualified._pin(ENTRY)


def _inputs():
    # Exact independently spelled frozen inputs. No expected output is read
    # from the entry's predictions or from producer input-plan.json.
    variants = (
        (("red","yellow","cyan"),"composite",None,(1,1,2),(True,False,True)),
        (("inverted_red","inverted_yellow","inverted_cyan"),"composite","Max",(1,2,2),(True,False,True)),
        (("inverted_red","yellow","custom_c"),"composite","Max",(1,1,2),(True,False,True)),
        (("red","constant","custom_b"),"composite",None,(1,1,2),(True,False,True)),
        (("red","last_step_only","custom_a"),"composite",None,(1,1,2),(True,False,True)),
        (("red","yellow","cyan"),"color","Max",(1,1,2),(False,True,False)),
        (("red","inverted_yellow","custom_c"),"grayscale","Max",(1,1,2),(True,False,True)),
        (("red","inverted_yellow","custom_c"),"color","Max",(1,1,2),(False,True,False)))
    result = {}
    for case,(tables,mode,projection,position,flags) in zip(CASE_IDS,variants):
        fixture = qualified._fixture(width=5,height=3,times=2,slices=4,position=position,
            ranges=((1,201),(7,219),(11,241)),tables=tables,mode=mode,projection=projection,flags=flags,
            formula=lambda t,c,z,y,x:40*t+30*c+5*z+2*y+x)
        actions = [qualified._action("main","setup.warmed"),
            qualified._action("main","before","visibility",render=False,value=list(flags)),
            qualified._action("main","choice.min","channels_choice",index=5),
            qualified._action("main","after.redraw")]
        if case in {"N01","N03","N07"}:
            actions += [qualified._action("main","choice.min.repeated","channels_choice",index=5),
                        qualified._action("main","after.repeated.redraw"),qualified._action("main","after.repeated.redraw2")]
        result[case] = fixture,actions
    return result


INPUTS = _inputs()


class _Expected(qualified._Expected):
    """Min-entry state machine; inherited render is pinned test-owned math."""
    def choose_min(self):
        old_mode = self.mode
        self.projection = "Min"
        transformed = not qualified._inverted(self.processor_table)
        if transformed:
            self.tables = [qualified._macro(table) for table in self.tables]
            self.position[1] = 0
            # A bound StackWindow updates C before each setSlice. Color and
            # Composite reinstall current C's table; Gray deliberately does
            # not. After its last setLut/Stack.setChannel(1), Gray therefore
            # retains the transformed C2 processor table while position is C0.
            self.processor_table = self.tables[-1] if old_mode == "grayscale" else self.tables[0]
            if not qualified._inverted(self.processor_table):
                self.projection = "Max"
        if old_mode != "composite":
            self.flags = [True]*8
        self.mode = "composite"
        return transformed


def _host(actual,expected,image_id,**kwargs):
    # Reuse structural checks only after checking the real new observer class.
    assert actual["host"]["window_class"] == JAVA_CLASS+"$HiddenStackWindow"
    adapted = {**actual,"host":{**actual["host"],"window_class":"CompositeMaxReference$HiddenStackWindow"}}
    return qualified._host(adapted,expected,image_id,**kwargs)


def _host_setup(setups,fixture):
    assert set(setups) == {"main"}
    setup = setups["main"]
    assert set(setup) == {"order","before_attachment","after_attachment","configured"}
    assert setup["order"] == qualified.HOST_ORDER
    initial = copy.deepcopy(fixture)
    initial.update(position_tcz=[0,0,0],mode="composite",projection=None,visibility=[True]*3)
    initial["channels"] = [{"requested_range":[0,255],"table_name":"reference_default","rgb_interleaved_hex":None} for _ in range(3)]
    expected = _Expected(initial)
    expected.processor_table = qualified._table("gray")
    image_id = setup["before_attachment"]["image_id"]
    assert setup["before_attachment"]["host"] is None
    qualified._state(setup["before_attachment"],expected,image_id)
    qualified._state(setup["after_attachment"],expected,image_id)
    identities = _host(setup["after_attachment"],expected,image_id,painted=0,last_label=None,configured=False)
    expected = _Expected(fixture)
    qualified._state(setup["configured"],expected,image_id)
    _host(setup["configured"],expected,image_id,painted=0,last_label=None,identities=identities)
    return identities


def _choice(value,*,count=None):
    assert JAVA_CLASS+"$HiddenChannels" in value["listeners"]
    adapted = {**value,"listeners":["CompositeMaxReference$HiddenChannels" if v == JAVA_CLASS+"$HiddenChannels" else v for v in value["listeners"]]}
    qualified._choice(adapted,count=count)


def _environment(value):
    # The parent helper's remaining environment/class/resource checks apply
    # unchanged. The scope string is checked here, then explicitly adapted.
    assert value["scope"] == "hidden StackWindow/ImageCanvas and real Channels Choice N01-N08; offscreen paint, no OS input"
    adapted = {**value,"scope":"programmatic CompositeImage R01-R11; hidden StackWindow/ImageCanvas and real Channels Choice R12-R18; offscreen paint, no OS input"}
    qualified._environment(adapted)


def assert_case(row):
    _reuse_identity()
    case = row["id"]
    assert case in CASE_IDS and row["contract"] == CONTRACT
    assert set(row) == {"contract","id","input_contract","host_setup","source_before","source_after","stages","environment","elapsed_ms"}
    fixture,actions = INPUTS[case]
    spec = row["input_contract"]
    assert set(spec) == {"id","driving_method","question","fixtures","actions"}
    assert spec["id"] == case and spec["fixtures"] == [fixture] and spec["actions"] == actions
    assert spec["driving_method"] == "actual_Channels_Choice_ItemEvent" and isinstance(spec["question"],str) and spec["question"]
    source = {"main":qualified._source(fixture)}
    assert row["source_before"] == row["source_after"] == source
    qualified._finite(row["elapsed_ms"],5000)
    _environment(row["environment"])
    assert len(row["stages"]) == STAGE_COUNTS[CASE_IDS.index(case)]
    host_ids = _host_setup(row["host_setup"],fixture)
    assert row["stages"][0]["before"] == row["host_setup"]["main"]["configured"]
    expected = _Expected(fixture)
    image_id = row["stages"][0]["before"]["image_id"]
    previous = None
    paint_count,rgb_count,dispatch_count = 0,0,0
    paint_label = None
    transitions = []
    for stage,action in zip(row["stages"],actions):
        assert set(stage) == {"label","input","before","after","rgb","dispatch","elapsed_ms"}
        assert stage["label"] == action["label"] and stage["input"] == action
        qualified._finite(stage["elapsed_ms"],5000)
        if previous is not None:
            assert stage["before"] == previous
        qualified._state(stage["before"],expected,image_id)
        _host(stage["before"],expected,image_id,painted=paint_count,last_label=paint_label,identities=host_ids)
        if action["operation"] == "visibility":
            expected.flags[:3] = action["value"]
            assert stage["dispatch"] is None
        elif action["operation"] == "channels_choice":
            delivered = stage["dispatch"]
            assert delivered["method"] == "Choice.dispatchEvent(ItemEvent)"
            assert delivered["event_id"] == 701 and delivered["state_change"] == 1
            assert delivered["event_item"] == "Composite Min" and delivered["requested_index"] == 5
            _choice(delivered["before"])
            before_choice = {"color":1,"grayscale":2}.get(expected.mode,
                {None:0,"Sum":0,"Max":4,"Min":5,"Invert":6}[expected.projection])
            assert delivered["before"]["selected_index"] == before_choice
            _choice(delivered["after"],count=delivered["before"]["delivered_item_events"]+1)
            assert delivered["source_identity"] == delivered["before"]["identity"] == delivered["after"]["identity"]
            assert delivered["current_image_id"] == image_id
            inverted = qualified._inverted(expected.processor_table)
            transformed = expected.choose_min()
            assert delivered["after"]["selected_index"] == {"Max":4,"Min":5}[expected.projection]
            transitions.append((inverted,transformed,expected.projection,expected.position[1]))
            qualified._state(delivered["state_after_handler_before_readback"],expected,image_id)
            _host(delivered["state_after_handler_before_readback"],expected,image_id,
                  painted=paint_count,last_label=paint_label,identities=host_ids)
            dispatch_count += 1
        else:
            assert action["operation"] == "observe" and stage["dispatch"] is None
        if action["render"]:
            wanted = expected.render()
            assert stage["rgb"]["shape_yx"] == [3,5]
            assert stage["rgb"]["method"] in {"BufferedImage.getRGB","PixelGrabber"}
            assert stage["rgb"]["rgb_interleaved_hex"] == wanted,(case,action["label"])
            rgb_count += 1;paint_count += 1;paint_label = action["label"]
        else:
            assert stage["rgb"] is None
        qualified._state(stage["after"],expected,image_id)
        _host(stage["after"],expected,image_id,painted=paint_count,last_label=paint_label,
              identities=host_ids,paint_completed=action["render"])
        if action["render"]:
            cache = stage["after"]["cache"]
            assert cache["image_present"] is True and cache["current_channel"] == expected.position[1]
            assert cache["rgb_pixels_present"] is (expected.mode == "composite")
            assert cache["channel_processors_present"] is (expected.mode == "composite")
        previous = stage["after"]
    # Source-derived literal transition predictions are independently frozen
    # here. Mismatches remain evidence to diagnose; no accepted-outcome sets.
    literal = {
        "N01":[(False,True,"Min",0),(True,False,"Min",0)],
        "N02":[(True,False,"Min",2)],
        "N03":[(False,True,"Max",0),(False,True,"Min",0)],
        "N04":[(True,False,"Min",1)],"N05":[(True,False,"Min",1)],
        "N06":[(False,True,"Min",0)],
        "N07":[(False,True,"Max",0),(False,True,"Max",0)],
        "N08":[(True,False,"Min",1)]}
    assert transitions == literal[case]
    assert rgb_count == paint_count == RGB_COUNTS[CASE_IDS.index(case)]
    assert dispatch_count == DISPATCH_COUNTS[CASE_IDS.index(case)]


def _raw(raw):
    assert isinstance(raw,bytes) and 0 < len(raw) <= MAX_PAYLOAD
    rows = [qualified._json(line) for line in raw.splitlines() if line.strip()]
    assert [row["id"] for row in rows] == list(CASE_IDS)
    assert sum(len(row["stages"]) for row in rows) == 41
    assert sum(stage["rgb"] is not None for row in rows for stage in row["stages"]) == 33
    assert sum(len(row["source_before"]) for row in rows) == 8
    dispatches = [stage["dispatch"] for row in rows for stage in row["stages"] if stage["dispatch"] is not None]
    assert len(dispatches) == 11
    for count,dispatch in enumerate(dispatches):
        _choice(dispatch["before"],count=count);_choice(dispatch["after"],count=count+1)
    assert len({dispatch["source_identity"] for dispatch in dispatches}) == 1
    assert sum(row["elapsed_ms"] for row in rows) <= 45_000
    return dict(zip(CASE_IDS,rows))


def _default_raw():
    return Path(os.environ.get("IMAGEJXX_COMPOSITE_MIN_REFERENCE",STAGE/"capture-001/reference.stdout")).resolve()


def _load(raw_path):
    _reuse_identity()
    raw_path = Path(raw_path).resolve()
    assert raw_path.is_relative_to(STAGE.resolve())
    receipt = qualified._json(qualified._bytes(raw_path.parent/"capture.json"))
    assert receipt["contract"] == CONTRACT and receipt["state"] == "raw_reference_observations_only_no_product_or_parity_acceptance"
    assert receipt["ordered_case_ids"] == list(CASE_IDS) and receipt["case_count"] == 8 and receipt["stage_count"] == 41
    qualified._finite(receipt["elapsed_seconds"],60)
    assert receipt["policy_decisions"] == [] and receipt["commercial_eligibility"] == "not_decided_by_capture_tool"
    raw = qualified._pin(receipt["raw"])
    assert (ROOT/receipt["raw"]["path"]).resolve() == raw_path
    process = receipt["process"]
    assert process["exit_code"] == 0 and 1 <= process["timeout_seconds"] <= 50
    assert process["stdout"] == receipt["raw"] and qualified._pin(process["stderr"]) == b""
    assert "-Xmx256m" in process["argv"] and "-Djava.awt.headless=false" in process["argv"]
    assert process["argv"][-2:] == [JAVA_CLASS,"--capture"]
    prepared = qualified._json(qualified._pin(receipt["prepared"]))
    assert prepared["contract"] == CONTRACT and prepared["state"] == "prepared_probe_only_N01_N08_not_executed"
    assert receipt["harness"] == prepared["harness"] and prepared["harness"]["sha256"] == PRODUCER_SHA
    qualified._pin(prepared["harness"])
    plan = qualified._json(qualified._pin(prepared["input_plan"]))
    assert plan["contract"] == CONTRACT and plan["ordered_case_ids"] == list(CASE_IDS) and plan["stage_count"] == 41
    assert plan["bounds"] == {"case_ids":8,"case_seconds":5,"prepare_parent_seconds":120,"capture_parent_seconds":60,
        "java_case_loop_seconds":45,"java_process_seconds":50,"compile_seconds":60,"probe_seconds":20,
        "java_heap_mib":256,"combined_payload_bytes":MAX_PAYLOAD,"fresh_stage_bytes":MAX_BUILD,
        "max_plane_pixels":15,"max_scalar_samples_per_fixture":360,"max_scalar_samples_per_case":360}
    membership = plan["output_membership"]
    assert membership["raw_jsonl_rows"] == 8 and membership["stages"] == 41 and membership["RGB_readbacks"] == 33
    assert membership["source_before_after_pairs"] == 8 and membership["real_Channels_dispatches"] == 11
    assert membership["per_case_stage_counts"] == dict(zip(CASE_IDS,STAGE_COUNTS))
    assert membership["per_case_RGB_counts"] == dict(zip(CASE_IDS,RGB_COUNTS))
    assert membership["RGB_per_stage"] == "all render=true stages; pre-handler nonrender visibility stages explicitly null"
    transition_host = {**qualified.TRANSITION_HOST,"case_ids":list(CASE_IDS),"window_class":JAVA_CLASS+"$HiddenStackWindow"}
    assert plan["environment_inputs"] == {"ImageJ_constructor":"ImageJ.NO_SHOW","Channels_presentation":"subclass show/setVisible only",
        "gui_scale":1,"auto_contrast":False,"use_inverting_lut":False,
        "current_image_binding":"WindowManager.setCurrentWindow for hosted cases and setTempCurrentImage on actual EDT",
        "transition_input":"actual Choice.dispatchEvent(ItemEvent.SELECTED, index 5)","transition_image_host":transition_host}
    assert plan["scope"] == "real hidden StackWindow/ImageCanvas plus Channels Choice dispatch N01-N08; raw reference only, no product/OS-input/commercial acceptance"
    observed = _raw(raw)
    assert plan["cases"] == [row["input_contract"] for row in observed.values()]
    canonical = qualified.json.dumps(plan["cases"],sort_keys=True,separators=(",",":"),allow_nan=False).encode()
    assert plan["cases_canonical_sha256"] == hashlib.sha256(canonical).hexdigest()
    assert plan["ordered_stage_labels"] == {case:[a["label"] for a in INPUTS[case][1]] for case in CASE_IDS}
    verified = qualified._json(qualified._pin(prepared["verified_inputs"]))
    expected_reference = {"version":"1.54r","commit":qualified.COMMIT,"source_archive_sha256":qualified.ARCHIVE_SHA,
                          "runtime_archive_sha256":qualified.RUNTIME_SHA}
    assert verified["reference"] == expected_reference
    assert [(q["bytes"],q["sha256"]) for q in verified["archives"]] == [(6146010,qualified.ARCHIVE_SHA),(705099776,qualified.RUNTIME_SHA)]
    old_raw = qualified._pin(verified["reused_receipt"])
    assert hashlib.sha256(old_raw).hexdigest() == qualified.RECEIPT_SHA
    old = qualified._json(old_raw)
    assert old["reference"] == expected_reference
    for key,original in (("source_tree","verified_source_tree"),("runtime_tree","verified_runtime_tree"),("reused_classes","compiled_classes")):
        assert verified[key] == qualified._manifest(old[original])
    assert set(verified["known_source_members"]) == set(SOURCE_PINS)
    for name,sha in SOURCE_PINS.items():
        assert verified["known_source_members"][name] == old["verified_source_tree"][name]
        assert verified["known_source_members"][name]["sha256"] == sha
    entry_pin = verified["entry_contract"]
    assert entry_pin["path"] == ENTRY["path"] and entry_pin["sha256"] == ENTRY["sha256"]
    qualified._pin(entry_pin)
    assert plan["entry_contract"] == entry_pin
    host_pin = verified["qualified_host_source"]
    assert host_pin["path"] == "scripts/capture_composite_max_reference.py" and host_pin["bytes"] == 65651
    assert host_pin["sha256"] == "210cb868a858857a37a579fc69674eeaab2bc03b7780cc4e1086d18c788b1ef0"
    qualified._pin(host_pin)
    assert plan["qualified_host_source"] == host_pin
    execution = qualified._json(qualified._pin(prepared["execution_inputs"]))
    assert execution["input_plan"] == prepared["input_plan"] and execution["verified_inputs"] == prepared["verified_inputs"]
    assert execution["driver"] == prepared["driver"]
    qualified._pin(prepared["driver"])
    assert hashlib.sha256(qualified._pin(execution["macro_resource"])).hexdigest() == qualified.MACRO_SHA
    assert execution["macro_origin"]["member"] == f"ImageJ-{qualified.COMMIT}/macros/InvertAllLuts.txt"
    assert execution["macro_origin"]["member_sha256"] == qualified.MACRO_SHA
    assert execution["macro_origin"]["archive"] == verified["archives"][0]
    for tool in execution["tools"]:
        qualified._pin(tool)
    assert set(prepared["prepared_classes"]) == {JAVA_CLASS+".class",JAVA_CLASS+"$HiddenChannels.class",
        JAVA_CLASS+"$HiddenStackWindow.class",JAVA_CLASS+"$Host.class","macros/InvertAllLuts.txt"}
    for name,q in prepared["prepared_classes"].items():
        relative = PurePosixPath(name)
        assert relative.as_posix() == name and not relative.is_absolute() and ".." not in relative.parts
        assert name.endswith(".class") or name == "macros/InvertAllLuts.txt"
        class_path = (ROOT/receipt["prepared"]["path"]).parent/"classes"/name
        content = qualified._bytes(class_path,MAX_BUILD)
        assert len(content) == q["bytes"] and hashlib.sha256(content).hexdigest() == q["sha256"]
    assert sum(q["bytes"] for q in prepared["prepared_classes"].values()) <= MAX_BUILD
    probe = prepared["probe"]
    _environment(probe["environment"]);_choice(probe["choice"],count=0)
    assert probe["kind"] == "environment_probe_no_N_dispatch"
    fixture = INPUTS["N01"][0]
    host_ids = _host_setup(probe["host_setup"],fixture)
    expected = _Expected(fixture)
    qualified._state(probe["state"],expected,probe["state"]["image_id"])
    _host(probe["state"],expected,probe["state"]["image_id"],painted=1,last_label="probe",identities=host_ids,paint_completed=True)
    for name,timeout in (("compile",60),("version",10),("probe",20)):
        held = prepared["processes"][name]
        assert held["exit_code"] == 0 and 1 <= held["timeout_seconds"] <= timeout
        qualified._pin(held["stdout"]);qualified._pin(held["stderr"])
        label = "javac" if name == "compile" else name
        recorded = qualified._json(qualified._bytes((ROOT/receipt["prepared"]["path"]).parent/(label+".command.json")))
        assert recorded["argv"] == held["argv"] and recorded["timeout_seconds"] == held["timeout_seconds"]
        qualified._finite(recorded["parent_deadline_remaining_seconds"],120)
        assert recorded["execution_scope"] == "bounded reference observer; no OS input or product acceptance"
    assert qualified._json(qualified._pin(prepared["processes"]["probe"]["stdout"])) == probe
    consumed = qualified._json(qualified._pin(receipt["execution_inputs"]))
    assert consumed["prepared"] == receipt["prepared"] and consumed["harness"] == prepared["harness"]
    for name in ("input_plan","verified_inputs","driver"):
        assert consumed[name] == prepared[name]
    assert consumed["ordered_case_ids"] == list(CASE_IDS) and consumed["stage_count"] == 41 and consumed["bounds"] == plan["bounds"]
    copied_plan = qualified._bytes(raw_path.parent/"input-plan.json")
    assert copied_plan == qualified._pin(prepared["input_plan"])
    command = qualified._json(qualified._bytes(raw_path.parent/"command.json"))
    assert set(command) == {"argv","maximum_process_seconds","parent_seconds","ordered_case_ids","input_plan","prepared"}
    assert command["argv"] == process["argv"] and command["maximum_process_seconds"] == 50 and command["parent_seconds"] == 60
    assert command["prepared"] == receipt["prepared"] and command["ordered_case_ids"] == list(CASE_IDS)
    assert (ROOT/command["input_plan"]["path"]).resolve() == raw_path.parent/"input-plan.json"
    assert qualified._pin(command["input_plan"]) == copied_plan
    recorded = qualified._json(qualified._bytes(raw_path.parent/"reference.command.json"))
    assert recorded["argv"] == process["argv"] and recorded["timeout_seconds"] == process["timeout_seconds"]
    qualified._finite(recorded["parent_deadline_remaining_seconds"],60)
    assert recorded["execution_scope"] == "bounded reference observer; no OS input or product acceptance"
    for stage in (raw_path.parent,(ROOT/receipt["prepared"]["path"]).parent):
        files = [path for path in stage.rglob("*") if path.is_file()]
        assert all(not path.is_symlink() for path in files)
        assert sum(path.stat().st_size for path in files) <= MAX_BUILD
        assert sum(path.stat().st_size for path in files if path.suffix in {".json",".stdout",".stderr"}) <= MAX_PAYLOAD
    return observed


@pytest.fixture(scope="module")
def reference_cases():
    return _load(_default_raw())


def assert_raw(raw):
    rows = _raw(raw if isinstance(raw,bytes) else qualified._bytes(Path(raw)))
    for row in rows.values():
        assert_case(row)
    return rows


def assert_capture(raw_path=None):
    rows = _load(_default_raw() if raw_path is None else raw_path)
    for row in rows.values():
        assert_case(row)
    return rows


@pytest.mark.parametrize("case_id",CASE_IDS)
def test_composite_min_reference(case_id,reference_cases):
    assert_case(reference_cases[case_id])
