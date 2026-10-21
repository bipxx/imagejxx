"""Owned consumer graphs cover complete native definitions and frozen effects.

These are synthetic producers and reports. They do not claim real consumers.
"""

from copy import deepcopy
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys

import pytest


ROOT = Path(__file__).resolve().parents[2]


def load_exact(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


consumer = load_exact("owned_parity_consumer_tests", ROOT / "scripts/parity_consumers.py")
surface = load_exact("owned_consumer_surface_builder", ROOT / "tests/python/test_parity_execution.py")
execution = consumer.execution
evidence = consumer.evidence
BINDING = "DB.owned-native-settings"
SNAPSHOT = "build/owned-evidence/consumer-assignments.json"
EXPORT_PATH = "build/owned-evidence/settings-schema.json"
SOURCE_PIN = "8f43e18718113d41a976dbed9336bde19f06c444bb0ba48b463ca362a50decf8"

# Pinned historical37-definition export is embedded below, not read from build
# output at test time. Owned traces retain all 37 definitions and all four contexts.
PINNED_EXPORT = json.loads('{"schema_version":1,"definitions":[{"name":"theme","group":"Appearance","label":"Theme","description":"Colors of desktop controls.","kind":"choice","default":"dark","minimum":null,"maximum":null,"choices":["dark","light","classic"]},{"name":"ui_scale","group":"Appearance","label":"UI and font scale","description":"Scale controls and text relative to the display scale.","kind":"float","default":1.0,"minimum":0.5,"maximum":3.0,"choices":[]},{"name":"auto_dpi","group":"Appearance","label":"Follow display DPI","description":"Apply the operating system display scale to controls and fonts.","kind":"bool","default":true,"minimum":null,"maximum":null,"choices":[]},{"name":"canvas_background","group":"Appearance","label":"Canvas background","description":"Color behind images in the image canvas.","kind":"color","default":"#181b21","minimum":null,"maximum":null,"choices":[]},{"name":"default_zoom","group":"Appearance","label":"Initial image zoom","description":"Zero fits new images; positive values are screen pixels per image X pixel.","kind":"float","default":0.0,"minimum":0.0,"maximum":64.0,"choices":[]},{"name":"interpolation","group":"Appearance","label":"Image interpolation","description":"How displayed images are sampled when zoomed; does not change pixels.","kind":"choice","default":"nearest","minimum":null,"maximum":null,"choices":["nearest","linear"]},{"name":"foreground","group":"Drawing","label":"Paint color","description":"Default color for fill, brush and annotations.","kind":"color","default":"#ffffff","minimum":null,"maximum":null,"choices":[]},{"name":"background","group":"Drawing","label":"Erase color","description":"Default color for clearing pixels and the eraser.","kind":"color","default":"#000000","minimum":null,"maximum":null,"choices":[]},{"name":"brush_width","group":"Drawing","label":"Brush width","description":"Brush and eraser diameter in image pixels.","kind":"float","default":8.0,"minimum":1.0,"maximum":256.0,"choices":[]},{"name":"line_width","group":"Drawing","label":"Line selection width","description":"Raster width of line, polyline and angle selections.","kind":"float","default":1.0,"minimum":1.0,"maximum":256.0,"choices":[]},{"name":"point_size","group":"Drawing","label":"Point size","description":"Raster diameter of point and multipoint selections.","kind":"float","default":8.0,"minimum":1.0,"maximum":256.0,"choices":[]},{"name":"text_size","group":"Drawing","label":"Text size","description":"Default annotation font size in image pixels.","kind":"int","default":16,"minimum":1.0,"maximum":256.0,"choices":[]},{"name":"show_rois","group":"Selection","label":"Show ROIs","description":"Display positioned saved ROI outlines.","kind":"bool","default":true,"minimum":null,"maximum":null,"choices":[]},{"name":"wand_tolerance","group":"Selection","label":"Wand tolerance","description":"Maximum intensity difference from the seed pixel.","kind":"float","default":0.0,"minimum":0.0,"maximum":1.7976931348623157e+308,"choices":[]},{"name":"wand_connectivity","group":"Selection","label":"Wand connectivity","description":"Use four or eight neighboring pixels for wand flood fill.","kind":"int","default":4,"minimum":4.0,"maximum":8.0,"choices":[]},{"name":"animation_fps","group":"Playback","label":"Frames per second","description":"Z-stack playback rate.","kind":"float","default":8.0,"minimum":0.10000000000000001,"maximum":120.0,"choices":[]},{"name":"conversion_scale","group":"Conversion","label":"Scale when converting","description":"Scale 16-bit/float to 8-bit and float to 16-bit with the chosen source range.","kind":"bool","default":true,"minimum":null,"maximum":null,"choices":[]},{"name":"conversion_weighted_rgb","group":"Conversion","label":"Weighted RGB conversion","description":"Use the configured RGB weights; otherwise use equal one-third weights.","kind":"bool","default":false,"minimum":null,"maximum":null,"choices":[]},{"name":"conversion_red_weight","group":"Conversion","label":"Red weight","description":"Red coefficient for weighted grayscale conversion.","kind":"float","default":0.29899999999999999,"minimum":-1.7976931348623157e+308,"maximum":1.7976931348623157e+308,"choices":[]},{"name":"conversion_green_weight","group":"Conversion","label":"Green weight","description":"Green coefficient for weighted grayscale conversion.","kind":"float","default":0.58699999999999997,"minimum":-1.7976931348623157e+308,"maximum":1.7976931348623157e+308,"choices":[]},{"name":"conversion_blue_weight","group":"Conversion","label":"Blue weight","description":"Blue coefficient for weighted grayscale conversion.","kind":"float","default":0.114,"minimum":-1.7976931348623157e+308,"maximum":1.7976931348623157e+308,"choices":[]},{"name":"conversion_range_policy","group":"Conversion","label":"Conversion source range","description":"Display captures the current range or unsigned 16-bit default, falling back to per-plane data. Data uses per-plane extrema. Dtype uses the native type range.","kind":"choice","default":"display","minimum":null,"maximum":null,"choices":["display","data","dtype"]},{"name":"conversion_full_range_inversions","group":"Conversion","label":"Full-range 16-bit inversion","description":"Invert unsigned 16-bit pixels using the configured bit range instead of observed image extrema.","kind":"bool","default":false,"minimum":null,"maximum":null,"choices":[]},{"name":"conversion_unsigned16_range","group":"Conversion","label":"Unsigned 16-bit range (bits)","description":"Auto uses data extrema for display conversion and 16 bits for full-range inversion. Fixed choices set the maximum to 2^bits-1.","kind":"choice","default":"auto","minimum":null,"maximum":null,"choices":["auto","8","10","12","14","15","16"]},{"name":"binary_iterations","group":"Binary processing","label":"Morphology iterations","description":"Repeat each erosion/dilation stage of the binary commands this many times.","kind":"int","default":1,"minimum":1.0,"maximum":100.0,"choices":[]},{"name":"binary_neighbors","group":"Binary processing","label":"Neighbor count","description":"Required adjacent background pixels for erosion or foreground pixels for dilation (eight-neighbor neighborhood).","kind":"int","default":1,"minimum":1.0,"maximum":8.0,"choices":[]},{"name":"binary_black_background","group":"Binary processing","label":"Black background","description":"Use foreground pixel value 255 and background 0; otherwise foreground is 0 and background 255.","kind":"bool","default":true,"minimum":null,"maximum":null,"choices":[]},{"name":"binary_pad_edges","group":"Binary processing","label":"Pad edges when eroding","description":"Treat pixels beyond the image boundary as foreground during binary erosion, including opening/closing stages.","kind":"bool","default":false,"minimum":null,"maximum":null,"choices":[]},{"name":"history_limit","group":"Resources","label":"Undo steps","description":"Maximum undo steps per document.","kind":"int","default":8,"minimum":1.0,"maximum":128.0,"choices":[]},{"name":"history_memory_mb","group":"Resources","label":"Undo pixel budget (MiB)","description":"Per-document budget for pixel buffers held only by undo/redo. Zero retains the step limit without a byte limit.","kind":"int","default":0,"minimum":0.0,"maximum":1048576.0,"choices":[]},{"name":"processing_workers","group":"Resources","label":"Processing workers","description":"Maximum simultaneous native processing jobs.","kind":"int","default":2,"minimum":1.0,"maximum":64.0,"choices":[]},{"name":"processing_memory_mb","group":"Resources","label":"Processing reservations (MiB)","description":"Budget for declared working-memory reservations of running jobs. Zero removes the reservation limit; retained documents and results are separate.","kind":"int","default":512,"minimum":0.0,"maximum":1048576.0,"choices":[]},{"name":"measurement_precision","group":"Measurements","label":"Decimal places","description":"Minus one preserves full precision; other values format the results view and CSV. Native results retain full precision.","kind":"int","default":-1,"minimum":-1.0,"maximum":15.0,"choices":[]},{"name":"measurements","group":"Measurements","label":"Results columns","description":"Choose visible/exported columns; an empty list shows every available column.","kind":"columns","default":[],"minimum":null,"maximum":null,"choices":["Image","Slice","count","minimum","maximum","mean","stddev","median","sum","integrated_density","Area","Unit","ROI","Bounds","Length","Angle","label","x","y","width","height","pixels","area","centroid_x","centroid_y","perimeter","eccentricity","circularity","touches_border"]},{"name":"profile_samples","group":"Measurements","label":"Profile samples per segment","description":"Zero samples every pixel interval; positive values specify samples per line segment.","kind":"int","default":0,"minimum":0.0,"maximum":1000000.0,"choices":[]},{"name":"print_dpi","group":"Files","label":"PDF resolution (DPI)","description":"Default physical resolution for PDF export.","kind":"float","default":150.0,"minimum":1.0,"maximum":9600.0,"choices":[]},{"name":"print_landscape","group":"Files","label":"Landscape PDF","description":"Default page orientation for PDF export.","kind":"bool","default":false,"minimum":null,"maximum":null,"choices":[]}],"defaults":{"animation_fps":8.0,"auto_dpi":true,"background":"#000000","binary_black_background":true,"binary_iterations":1,"binary_neighbors":1,"binary_pad_edges":false,"brush_width":8.0,"canvas_background":"#181b21","conversion_blue_weight":0.114,"conversion_full_range_inversions":false,"conversion_green_weight":0.58699999999999997,"conversion_range_policy":"display","conversion_red_weight":0.29899999999999999,"conversion_scale":true,"conversion_unsigned16_range":"auto","conversion_weighted_rgb":false,"default_zoom":0.0,"foreground":"#ffffff","history_limit":8,"history_memory_mb":0,"interpolation":"nearest","line_width":1.0,"measurement_precision":-1,"measurements":[],"point_size":8.0,"print_dpi":150.0,"print_landscape":false,"processing_memory_mb":512,"processing_workers":2,"profile_samples":0,"show_rois":true,"text_size":16,"theme":"dark","ui_scale":1.0,"wand_connectivity":4,"wand_tolerance":0.0}}')


def encoded(value):
    return surface.encoded(value)


def digest(value):
    return hashlib.sha256(value).hexdigest()


def changed_value(definition):
    name, kind, default = definition["name"], definition["kind"], definition["default"]
    if kind == "bool":
        return not default
    if kind == "choice":
        return next(value for value in definition["choices"] if value != default)
    if kind == "color":
        return "#123456" if default != "#123456" else "#abcdef"
    if kind == "columns":
        return ["owned_advisory_column"]
    if name == "wand_connectivity":
        return 8
    if name == "profile_samples":
        return 2
    return default + (1 if kind == "int" else 0.1)


def value_rule(assertion_id, value, behavior=False, ui=False):
    if isinstance(value, list):
        row = {"id": assertion_id, "rule": "sha256_equal", "expected": digest(execution.canonical(value))}
    else:
        row = {"id": assertion_id, "rule": "scalar_equal", "value_type": {bool: "boolean", int: "integer", float: "number", str: "string", type(None): "null"}[type(value)], "expected": value}
    row["observation"] = "behavior" if behavior else "identity"
    if ui:
        row["ui_state"] = True
    return row


def owner_requirement(documents):
    return documents["commands.json"]["requirements"][0]


def assignment(documents, key="binary_iterations"):
    return next(row for row in documents["commands.json"]["settings_consumer_assignments"]["entries"] if row["setting_key"] == key)


def contract(documents, key="binary_iterations"):
    return next(row for row in documents["commands.json"]["settings_consumer_contracts"] if row["definition"]["name"] == key)


def binding(documents):
    return documents["commands.json"]["settings_definition_bindings"][0]


def run(inventory):
    return inventory["verification_runs"][0]


def rebind(root, documents, inventory):
    documents["commands.json"]["run_bindings"][0]["inventory_run_sha256"] = evidence.run_fingerprint(run(inventory))


def sync_snapshot(root, documents, inventory, *, status="complete"):
    entries = documents["commands.json"]["settings_consumer_assignments"]["entries"]
    paths = {"src/native/settings.cpp", "include/imagejxx/settings.hpp"} | {file["path"] for row in entries for file in row["source_refs"]}
    pins = [{"path": path, "sha256": digest((root / path).read_bytes())} for path in sorted(paths)]
    value = {"schema_version": 1, "enumeration_status": status, "exposed_definition_names": list(binding(documents)["frozen_export"]["defaults"]), "sources": pins, "ownership": consumer.ownership_snapshot({row["id"]: row for row in documents["commands.json"]["requirements"]}), "assignments": deepcopy(entries)}
    payload = encoded(value)
    (root / SNAPSHOT).write_bytes(payload)
    identity = {"path": SNAPSHOT, "sha256": digest(payload)}
    run(inventory)["settings_consumer_assignment_snapshot"] = identity
    documents["commands.json"]["settings_consumer_assignments"]["snapshot"] = deepcopy(identity)
    rebind(root, documents, inventory)


def sync_contracts(documents):
    entries = {row["id"]: row for row in documents["commands.json"]["settings_consumer_assignments"]["entries"]}
    for row in documents["commands.json"]["settings_consumer_contracts"]:
        row["assignment"] = deepcopy(entries[row["assignment_id"]])
        row["definition_binding"] = deepcopy(binding(documents))
        row["definition"] = deepcopy(next(value for value in binding(documents)["frozen_export"]["definitions"] if value["name"] == row["assignment"]["setting_key"]))
        row["sha256"] = consumer.contract_fingerprint(row)


def sync_surfaces(root, documents, inventory):
    frozen = documents["commands.json"]["surface_contracts"][0]
    frozen["snapshot"] = execution.frozen_snapshot(owner_requirement(documents))
    frozen["sha256"] = execution.contract_fingerprint(frozen)
    surface.align_traces_with_contract(root, documents, inventory)
    for row in documents["commands.json"]["settings_consumer_contracts"]:
        for value in row["contexts"].values():
            value["surface_contract_sha256"] = frozen["sha256"]
        row["sha256"] = consumer.contract_fingerprint(row)


def owned_consumers(tmp_path, *, persisted=False):
    root, documents, inventory = surface.owned_execution(tmp_path)
    child = owner_requirement(documents)
    definitions = deepcopy(PINNED_EXPORT["definitions"])
    child["settings_consumers"] = [{"key": row["name"], "consumer": "Owned Settings option snapshot and frozen workflow"} for row in definitions]
    child["interfaces"] = {name: {"applicable": True, "entry_points": ["Operation form: binary_erode" if name == "imgui" else "binary_erode"], "evidence": []} for name in ("cpp", "python", "imgui")}
    identities = {}
    files = {
        "src/native/settings.cpp": (ROOT / "tests/fixtures/settings-consumer-baseline.cpp").read_bytes(),
        "include/imagejxx/settings.hpp": (ROOT / "include/imagejxx/settings.hpp").read_bytes(),
        "examples/cpp/settings_schema.cpp": b"Owned synthetic exporter source.\n",
        "examples/cpp/CMakeLists.txt": b"Owned synthetic exporter registration.\n",
        "sdk/include/imagejxx/settings.hpp": (ROOT / "include/imagejxx/settings.hpp").read_bytes(),
        "sdk/lib/imagejxx_core.lib": b"Owned synthetic native library artifact.\n",
        "owned/imagejxx_settings_schema_example.exe": b"Owned synthetic executed exporter artifact.\n",
        EXPORT_PATH: encoded(PINNED_EXPORT),
    }
    for path, payload in files.items():
        target = root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payload)
        identities[path] = digest(payload)
    roles = {"definition_sources": ["src/native/settings.cpp", "include/imagejxx/settings.hpp", "sdk/include/imagejxx/settings.hpp"], "producer_sources": ["examples/cpp/settings_schema.cpp", "examples/cpp/CMakeLists.txt"], "libraries": ["sdk/lib/imagejxx_core.lib"], "executables": ["owned/imagejxx_settings_schema_example.exe"], "output": EXPORT_PATH}
    exporter_case = {"registered_test_id": 1, "name": "owned_sdk"}
    run(inventory)["settings_export"] = {"definitions_compared": 37, "defaults_compared": 37, "output": EXPORT_PATH, "source_library_executable_output_sha256": identities, "identity_roles": roles, "report_role": "sdk", "report_case_identity": exporter_case}
    native_binding = {"id": BINDING, "run_id": surface.base.RUN, "report_role": "sdk", "report_case_identity": deepcopy(exporter_case), "export": {"path": EXPORT_PATH, "sha256": identities[EXPORT_PATH]}, "identities": deepcopy(identities), "identity_roles": deepcopy(roles), "frozen_export": deepcopy(PINNED_EXPORT)}
    source_refs = [{"path": "src/native/settings.cpp", "sha256": identities["src/native/settings.cpp"]}]
    commands = documents["commands.json"]
    commands.update(settings_consumers_schema_version=1, settings_definition_bindings=[native_binding], settings_consumer_assignments={"run_id": surface.base.RUN, "snapshot": {}, "entries": []}, settings_consumer_contracts=[], settings_consumer_proofs=[])
    frozen_surface = commands["surface_contracts"][0]
    frozen_surface["contexts"] = {context: [] for context in surface.CONTEXTS}
    for definition in definitions:
        key = definition["name"]
        default, changed = deepcopy(definition["default"]), changed_value(definition)
        applicable = {"default", "changed", "frozen", "override"} | ({"persisted"} if persisted else set())
        declaration = {scenario: ({"applicable": True} if scenario in applicable else {"applicable": False, "reason": "Owned detached options have no configuration file adapter.", "source_refs": deepcopy(source_refs)}) for scenario in consumer.SCENARIOS}
        entry = {"id": "A." + key, "definition_binding_id": BINDING, "setting_key": key, "consumer": {"api": "Settings", "member": key, "operation": "binary_erode"}, "requirement_ids": [child["id"]], "source_refs": deepcopy(source_refs), "consumption_mode": "configuration_file" if persisted else "detached_options", "contexts": {context: {"scenarios": deepcopy(declaration)} for context in surface.CONTEXTS}}
        commands["settings_consumer_assignments"]["entries"].append(entry)
        row = {"id": "CC." + key, "assignment_id": entry["id"], "assignment": deepcopy(entry), "definition_binding": deepcopy(native_binding), "definition": deepcopy(definition), "contexts": {}}
        for context in surface.CONTEXTS:
            scenarios = {}
            for scenario in sorted(applicable):
                captured = changed if scenario in {"changed", "persisted"} else default
                current = changed if scenario in {"changed", "frozen", "persisted"} else default
                effective = changed if scenario in {"changed", "override", "persisted"} else default
                values = {"setting": captured, "current": current, "effective": effective}
                prefix = f"{key}.{scenario}."
                ids = {name: prefix + name for name in (*values, "effect")}
                item = {"setting_value": deepcopy(captured), "current_setting_value": deepcopy(current), "effective_parameter_value": deepcopy(effective), "assertion_ids": ids}
                if scenario == "frozen":
                    item["captured_scenario"] = "default"
                if scenario == "override":
                    item["override_value"] = deepcopy(changed)
                    values["override"] = changed
                    ids["override"] = prefix + "override"
                if scenario == "persisted":
                    file_path = f"build/owned-evidence/preferences-{context}-{key}.json"
                    payload = encoded({key: changed})
                    (root / file_path).write_bytes(payload)
                    item["persistence"] = {"path": file_path, "sha256": digest(payload)}
                    ids["persistence_file"] = prefix + "persistence_file"
                    ids["reopened_setting"] = prefix + "reopened_setting"
                    values["reopened_setting"] = changed
                    frozen_surface["contexts"][context].append({"id": ids["persistence_file"], "rule": "sha256_equal", "expected": digest(payload), "observation": "behavior"})
                    surface.trace_entry(inventory, context)["producer"]["artifacts"].append(deepcopy(item["persistence"]))
                for name, value in values.items():
                    frozen_surface["contexts"][context].append(value_rule(ids[name], value))
                # Synthetic computed output is a distinct digest of the owned
                # key/value state, never the setting-value assertion itself.
                frozen_surface["contexts"][context].append({"id": ids["effect"], "rule": "sha256_equal", "expected": digest(encoded({"owned_computed_output": key, "value": effective})), "observation": "behavior", **({"ui_state": True} if context.startswith("imgui_") else {})})
                scenarios[scenario] = item
            row["contexts"][context] = {"requirement_id": child["id"], "surface_contract_id": frozen_surface["id"], "surface_contract_sha256": "0" * 64, "scenarios": scenarios}
            commands["settings_consumer_proofs"].append({"id": f"CP.{key}.{context}", "assignment_id": entry["id"], "contract_id": row["id"], "context": context, "surface_proof_id": "proof." + context})
        commands["settings_consumer_contracts"].append(row)
    sync_snapshot(root, documents, inventory)
    sync_surfaces(root, documents, inventory)
    sync_contracts(documents)
    rebind(root, documents, inventory)
    return root, documents, inventory


def validate(graph, strict=False):
    root, documents, inventory = graph
    return consumer.validate_consumers(root, documents, inventory, require_consumers=strict)


def partial_owner_graph(tmp_path):
    """One reviewed assignment, 36 known owners of 12 exposed setting keys."""
    graph = owned_consumers(tmp_path, persisted=True)
    _, documents, _ = graph
    commands = documents["commands.json"]
    first = owner_requirement(documents)
    keys = (
        "binary_iterations", "binary_neighbors", "binary_black_background",
        "binary_pad_edges", "conversion_blue_weight",
        "conversion_full_range_inversions", "conversion_green_weight",
        "conversion_range_policy", "conversion_red_weight", "conversion_scale",
        "conversion_unsigned16_range", "conversion_weighted_rgb",
    )
    template = deepcopy(first)
    children = []
    for index, key in enumerate(keys * 3):
        child = first if index == 0 else deepcopy(template)
        if index:
            child["id"] = f"{first['id']}-pending-{index:03d}"
        child["settings_consumers"] = [{"key": key, "consumer": "Owned Settings snapshot, frozen workflow and preference reopen"}]
        children.append(child)
    commands["requirements"] = children
    documents["fixtures.json"]["fixtures"][0]["requirement_ids"] = [child["id"] for child in children]
    commands["settings_consumer_assignments"]["entries"] = [assignment(documents)]
    commands["settings_consumer_contracts"] = [contract(documents)]
    commands["settings_consumer_proofs"] = [row for row in commands["settings_consumer_proofs"] if row["assignment_id"] == "A.binary_iterations"]
    assignment(documents)["consumption_mode"] = "detached_options"
    sync_snapshot(*graph, status="partial")
    sync_surfaces(*graph)
    sync_contracts(documents)
    return graph


def test_partial_assignment_retains_all_known_owner_and_context_obligations(tmp_path):
    graph = partial_owner_graph(tmp_path)
    result = validate(graph)
    assert len(result["exposed_keys"]) == 37
    assert result["required_consumers"] == 36
    assert result["required_contexts"] == 144
    assert result["claimed_consumers"] == 1
    assert result["verified_contexts"] == 4
    assert len(result["unresolved_enumeration"]) == 26
    assert len(result["missing_contracts"]) == 35
    assert len(result["missing_proofs"]) == 140
    pending = graph[1]["commands.json"]["requirements"][12]
    assert pending["settings_consumers"][0]["key"] == "binary_iterations"
    assert pending["id"] != owner_requirement(graph[1])["id"]
    assert any(row.get("requirement_id") == pending["id"] for row in result["missing_contracts"])
    assert {row["context"] for row in result["missing_proofs"] if row.get("requirement_id") == pending["id"]} == set(surface.CONTEXTS)
    with pytest.raises(ValueError, match="evidence required"):
        validate(graph, strict=True)


def test_partial_assignment_can_retain_pending_state_dependent_parameter(tmp_path):
    graph = partial_owner_graph(tmp_path)
    pending = next(child for child in graph[1]["commands.json"]["requirements"] if child["settings_consumers"][0]["key"] == "conversion_range_policy")
    # The native setting's display default requires document state before it
    # can become this API parameter. No pending assignment invents a relation.
    pending["contract"]["parameters"] = [{"name": "range_policy", "type": "string", "default": "data", "choices": ["data", "dtype", "explicit"], "units": "", "enabled_when": "always"}]
    sync_snapshot(*graph, status="partial")
    result = validate(graph)
    assert result["required_consumers"] == 36
    assert result["required_contexts"] == 144
    assert result["verified_contexts"] == 4
    assert any(row.get("requirement_id") == pending["id"] for row in result["missing_contracts"])
    assert len(graph[1]["commands.json"]["settings_consumer_assignments"]["entries"]) == 1
    with pytest.raises(ValueError, match="evidence required"):
        validate(graph, strict=True)


def test_removing_first_assignment_from_partial_snapshot_retains_required_scope(tmp_path):
    graph = partial_owner_graph(tmp_path)
    commands = graph[1]["commands.json"]
    commands["settings_consumer_assignments"]["entries"] = []
    commands["settings_consumer_contracts"] = []
    commands["settings_consumer_proofs"] = []
    sync_snapshot(*graph, status="partial")
    result = validate(graph)
    assert result["required_consumers"] == 36
    assert result["required_contexts"] == 144
    assert result["claimed_consumers"] == result["verified_contexts"] == 0
    assert len(result["unresolved_enumeration"]) == 26
    assert len(result["missing_contracts"]) == 36
    assert len(result["missing_proofs"]) == 144
    with pytest.raises(ValueError, match="evidence required"):
        validate(graph, strict=True)


def test_complete_enumeration_cannot_hide_pending_owner_assignment(tmp_path):
    graph = owned_consumers(tmp_path)
    commands = graph[1]["commands.json"]
    commands["settings_consumer_assignments"]["entries"].remove(assignment(graph[1]))
    commands["settings_consumer_contracts"].remove(contract(graph[1]))
    commands["settings_consumer_proofs"] = [row for row in commands["settings_consumer_proofs"] if row["assignment_id"] != "A.binary_iterations"]
    sync_snapshot(*graph)
    result = validate(graph)
    assert result["required_consumers"] == 37
    assert result["required_contexts"] == 148
    assert result["verified_contexts"] == 144
    assert not result["unresolved_enumeration"]
    assert len(result["missing_contracts"]) == 1
    assert len(result["missing_proofs"]) == 4
    with pytest.raises(ValueError, match="evidence required"):
        validate(graph, strict=True)


def test_partial_snapshot_cannot_omit_pending_owner(tmp_path):
    graph = partial_owner_graph(tmp_path)
    value = json.loads((graph[0] / SNAPSHOT).read_bytes())
    pending_id = graph[1]["commands.json"]["requirements"][12]["id"]
    value["ownership"] = [row for row in value["ownership"] if row["requirement_id"] != pending_id]
    rewrite_snapshot(graph, value)
    with pytest.raises(ValueError, match="ownership text or frozen interfaces"):
        validate(graph)


def test_complete_graph_covers_all_37_native_definitions_and_four_contexts(tmp_path):
    graph = owned_consumers(tmp_path)
    result = validate(graph, strict=True)
    assert len(result["exposed_keys"]) == result["required_consumers"] == result["claimed_consumers"] == 37
    assert result["required_contexts"] == result["verified_contexts"] == 148
    assert not result["unresolved_enumeration"]
    assert not result["missing_contracts"]
    assert not result["missing_proofs"]
    assert not result["unavailable_evidence"]
    assert digest((graph[0] / "src/native/settings.cpp").read_bytes()) == SOURCE_PIN


def test_complete_persistence_includes_file_reopen_and_computed_effect(tmp_path):
    result = validate(owned_consumers(tmp_path, persisted=True), strict=True)
    assert result["verified_contexts"] == 148


@pytest.mark.parametrize("field", sorted(consumer.EXTENSION))
def test_extension_is_all_or_none(tmp_path, field):
    graph = owned_consumers(tmp_path)
    del graph[1]["commands.json"][field]
    with pytest.raises(ValueError, match="incomplete settings consumer extension"):
        validate(graph)


def test_legacy_records_and_surface_digest_remain_unchanged(tmp_path):
    root, documents, inventory = surface.owned_execution(tmp_path)
    before = deepcopy(documents)
    fingerprint = documents["commands.json"]["surface_contracts"][0]["sha256"]
    result = consumer.validate_consumers(root, documents, inventory)
    assert documents == before
    assert result["verified_contexts"] == 0
    assert result["unresolved_enumeration"]
    assert fingerprint == execution.contract_fingerprint(documents["commands.json"]["surface_contracts"][0])


def test_candidate_only_legacy_graph_needs_no_inventory(tmp_path):
    documents = surface.base.legacy_documents()
    before = deepcopy(documents)
    result = consumer.validate_consumers(tmp_path, documents)
    assert result["required_contexts"] == 0
    assert result["unresolved_enumeration"]
    assert documents == before
    with pytest.raises(ValueError, match="evidence required"):
        consumer.validate_consumers(tmp_path, documents, require_consumers=True)


def test_empty_explicit_inventory_cannot_certify_unknown_native_consumer_scope(tmp_path):
    documents = surface.base.legacy_documents()
    inventory = {"schema_version": 1, "capabilities": [], "verification_runs": []}
    result = consumer.validate_consumers(tmp_path, documents, inventory)
    assert result["unresolved_enumeration"]
    assert result["verified_contexts"] == 0
    with pytest.raises(ValueError, match="evidence required"):
        consumer.validate_consumers(tmp_path, documents, inventory, require_consumers=True)


def test_repository_cli_retains_raw_scijava_priority_and_reports_partial_scope(tmp_path):
    paths = sorted((ROOT / "docs/parity").glob("*.json"))
    before = {path: path.read_bytes() for path in paths}
    commands = json.loads(before[ROOT / "docs/parity/commands.json"])
    priority = next(row["descriptor"]["values"]["priority"] for row in commands["candidates"] if row["id"] == "R-54a632549fbc1c07df466cce")
    assert priority == float("inf")
    result = subprocess.run(
        [sys.executable, str(ROOT / "scripts/parity_consumers.py"), "--root", str(ROOT)],
        cwd=tmp_path, capture_output=True, text=True, encoding="utf-8", timeout=60,
    )
    assert result.returncode == 0, result.stderr
    assert "37 exposed keys;" in result.stdout
    assert "contracts;" in result.stdout
    assert "contexts verified;" in result.stdout
    assert "enumeration gaps;" in result.stdout
    assert before == {path: path.read_bytes() for path in paths}


def test_repository_cli_strict_rejects_incomplete_consumers_after_legacy_loading(tmp_path):
    paths = sorted((ROOT / "docs/parity").glob("*.json"))
    before = {path: path.read_bytes() for path in paths}
    documents = {
        name: json.loads((ROOT / "docs/parity" / name).read_text(encoding="utf-8"))
        for name in (*consumer.schema.REFERENCE_FILES, *consumer.schema.COLLECTIONS)
        if (ROOT / "docs/parity" / name).is_file()
    }
    diagnostics = consumer.validate_consumers(ROOT, documents)
    assert diagnostics["unresolved_enumeration"]
    assert diagnostics["missing_proofs"]
    assert diagnostics["verified_contexts"] < diagnostics["required_contexts"]
    result = subprocess.run(
        [sys.executable, str(ROOT / "scripts/parity_consumers.py"), "--root", str(ROOT), "--require-consumers"],
        cwd=tmp_path, capture_output=True, text=True, encoding="utf-8", timeout=60,
    )
    assert result.returncode == 1
    assert "Settings consumer check failed:" in result.stderr
    assert "evidence required:" in result.stderr
    assert "incomplete" in result.stderr
    assert "nonfinite JSON" not in result.stderr
    assert before == {path: path.read_bytes() for path in paths}


@pytest.mark.parametrize("category", ["definition_sources", "producer_sources", "libraries", "executables", "output"])
def test_full_accepted_provenance_cannot_be_truncated(tmp_path, category):
    graph = owned_consumers(tmp_path)
    roles = binding(graph[1])["identity_roles"]
    path = roles[category] if category == "output" else roles[category][0]
    del binding(graph[1])["identities"][path]
    with pytest.raises(ValueError, match="complete accepted identity set"):
        validate(graph)


@pytest.mark.parametrize("field", ["report_role", "report_case_identity", "identity_roles"])
def test_historical_missing_case_roles_are_explicit_unverified_gaps(tmp_path, field):
    graph = owned_consumers(tmp_path)
    for name in ("report_role", "report_case_identity", "identity_roles"):
        del run(graph[2])["settings_export"][name]
    rebind(*graph)
    result = validate(graph)
    assert result["verified_contexts"] == 0
    assert any(gap["kind"] == "exporter_declaration" for gap in result["unavailable_evidence"])
    with pytest.raises(ValueError, match="evidence required"):
        validate(graph, strict=True)


def test_arbitrary_passing_sdk_case_cannot_replace_accepted_exporter_case(tmp_path):
    graph = owned_consumers(tmp_path)
    binding(graph[1])["report_case_identity"] = {"registered_test_id": 1, "name": "another_passing_case"}
    with pytest.raises(ValueError, match="exact accepted exporter case"):
        validate(graph)


@pytest.mark.parametrize("counter", ["definitions_compared", "defaults_compared"])
@pytest.mark.parametrize("value", [True, 37.0, 0, 36])
def test_export_counts_are_exact_positive_integer_counters(tmp_path, counter, value):
    graph = owned_consumers(tmp_path)
    run(graph[2])["settings_export"][counter] = value
    rebind(*graph)
    with pytest.raises(ValueError):
        validate(graph)


def test_frozen_export_cannot_omit_a_definition_and_its_matching_default(tmp_path):
    graph = owned_consumers(tmp_path)
    frozen = binding(graph[1])["frozen_export"]
    removed = frozen["definitions"].pop()
    del frozen["defaults"][removed["name"]]
    with pytest.raises(ValueError, match="truncates accepted"):
        validate(graph)


@pytest.mark.parametrize("value", [True, 1.0, 0, 101])
def test_native_integer_setting_values_reject_bool_float_and_bounds(tmp_path, value):
    graph = owned_consumers(tmp_path)
    row = contract(graph[1])
    row["contexts"]["cpp_api"]["scenarios"]["changed"]["setting_value"] = value
    row["sha256"] = consumer.contract_fingerprint(row)
    with pytest.raises(ValueError):
        validate(graph)


@pytest.mark.parametrize("kind,value", [("int", True), ("float", 1), ("bool", 1), ("choice", "invented"), ("color", "red"), ("columns", ["same", "same"])])
def test_frozen_definition_defaults_keep_native_typed_validation(tmp_path, kind, value):
    graph = owned_consumers(tmp_path)
    frozen = binding(graph[1])["frozen_export"]
    definition = next(row for row in frozen["definitions"] if row["kind"] == kind)
    definition["default"] = value
    frozen["defaults"][definition["name"]] = value
    with pytest.raises(ValueError):
        validate(graph)


def test_columns_suggestions_are_advisory_but_list_digest_is_binding(tmp_path):
    graph = owned_consumers(tmp_path)
    row = contract(graph[1], "measurements")
    assert row["contexts"]["cpp_api"]["scenarios"]["changed"]["setting_value"] == ["owned_advisory_column"]
    assert validate(graph, strict=True)["verified_contexts"] == 148


@pytest.mark.parametrize("key,value", [("wand_connectivity", 5), ("profile_samples", 1)])
def test_native_exceptions_are_not_replaced_by_bounds(tmp_path, key, value):
    graph = owned_consumers(tmp_path)
    row = contract(graph[1], key)
    row["contexts"]["cpp_api"]["scenarios"]["changed"]["setting_value"] = value
    row["sha256"] = consumer.contract_fingerprint(row)
    with pytest.raises(ValueError):
        validate(graph)


@pytest.mark.parametrize("collection", ["settings_definition_bindings", "settings_consumer_contracts", "settings_consumer_proofs"])
def test_duplicate_stable_ids_reject(tmp_path, collection):
    graph = owned_consumers(tmp_path)
    rows = graph[1]["commands.json"][collection]
    rows.append(deepcopy(rows[0]))
    with pytest.raises(ValueError, match="duplicate"):
        validate(graph)


def test_unknown_definition_or_orphan_consumer_rejects(tmp_path):
    graph = owned_consumers(tmp_path)
    assignment(graph[1])["setting_key"] = "unknown_native_setting"
    sync_snapshot(*graph)
    with pytest.raises(ValueError, match="unknown native definition"):
        validate(graph)


@pytest.mark.parametrize("context", surface.CONTEXTS)
def test_every_context_is_required_independently(tmp_path, context):
    graph = owned_consumers(tmp_path)
    row = assignment(graph[1])
    del row["contexts"][context]
    sync_snapshot(*graph)
    with pytest.raises(ValueError, match="interface applicability"):
        validate(graph)


@pytest.mark.parametrize("scenario", ["default", "changed", "frozen", "override"])
def test_missing_required_scenario_rejects(tmp_path, scenario):
    graph = owned_consumers(tmp_path)
    row = contract(graph[1])
    del row["contexts"]["cpp_api"]["scenarios"][scenario]
    row["sha256"] = consumer.contract_fingerprint(row)
    with pytest.raises(ValueError, match="omits applicable scenarios"):
        validate(graph)


def test_removing_assignment_without_changing_independent_snapshot_rejects(tmp_path):
    graph = owned_consumers(tmp_path)
    graph[1]["commands.json"]["settings_consumer_assignments"]["entries"].remove(assignment(graph[1]))
    with pytest.raises(ValueError, match="independent snapshot"):
        validate(graph)


def test_removing_contract_exposes_required_owner_gap(tmp_path):
    graph = owned_consumers(tmp_path)
    graph[1]["commands.json"]["settings_consumer_contracts"].remove(contract(graph[1]))
    graph[1]["commands.json"]["settings_consumer_proofs"] = [row for row in graph[1]["commands.json"]["settings_consumer_proofs"] if row["assignment_id"] != "A.binary_iterations"]
    result = validate(graph)
    assert len(result["missing_contracts"]) == 1
    assert len(result["missing_proofs"]) == 4
    assert result["verified_contexts"] == 144
    with pytest.raises(ValueError, match="evidence required"):
        validate(graph, strict=True)


def test_current_owner_text_cannot_be_erased_by_a_self_consistent_snapshot(tmp_path):
    graph = owned_consumers(tmp_path)
    value = json.loads((graph[0] / SNAPSHOT).read_bytes())
    value["ownership"] = []
    rewrite_snapshot(graph, value)
    with pytest.raises(ValueError, match="ownership text or frozen interfaces"):
        validate(graph)


def rewrite_snapshot(graph, value):
    payload = encoded(value)
    (graph[0] / SNAPSHOT).write_bytes(payload)
    identity = {"path": SNAPSHOT, "sha256": digest(payload)}
    run(graph[2])["settings_consumer_assignment_snapshot"] = identity
    graph[1]["commands.json"]["settings_consumer_assignments"]["snapshot"] = deepcopy(identity)
    rebind(*graph)


def test_partial_enumeration_cannot_certify_consumer_coverage(tmp_path):
    graph = owned_consumers(tmp_path)
    sync_snapshot(*graph, status="partial")
    result = validate(graph)
    assert result["unresolved_enumeration"]
    with pytest.raises(ValueError, match="evidence required"):
        validate(graph, strict=True)


def test_unassigned_exported_key_remains_gap_even_without_existing_child_text(tmp_path):
    graph = owned_consumers(tmp_path)
    commands = graph[1]["commands.json"]
    owner_requirement(graph[1])["settings_consumers"] = [row for row in owner_requirement(graph[1])["settings_consumers"] if row["key"] != "theme"]
    commands["settings_consumer_assignments"]["entries"].remove(assignment(graph[1], "theme"))
    commands["settings_consumer_contracts"].remove(contract(graph[1], "theme"))
    commands["settings_consumer_proofs"] = [row for row in commands["settings_consumer_proofs"] if row["assignment_id"] != "A.theme"]
    sync_snapshot(*graph)
    result = validate(graph)
    assert any(gap.get("setting_key") == "theme" for gap in result["unresolved_enumeration"])
    with pytest.raises(ValueError, match="evidence required"):
        validate(graph, strict=True)


@pytest.mark.parametrize("imgui", [True, False])
def test_non_erosion_required_owner_cannot_disappear(tmp_path, imgui):
    graph = owned_consumers(tmp_path)
    child = deepcopy(owner_requirement(graph[1]))
    child["id"] = "G052.owned-dilation-iterations"
    child["settings_consumers"] = [{"key": "binary_iterations", "consumer": "Settings snapshot and frozen workflow for dilation"}]
    child["interfaces"] = {name: {"applicable": True, "entry_points": ["binary_dilate"], "evidence": []} for name in ("cpp", "python", "imgui")}
    if not imgui:
        child["interfaces"]["imgui"] = {"applicable": False, "entry_points": [], "evidence": [], "inapplicable_reason": "Owned dilation consumer exposes only the two headless APIs."}
    graph[1]["commands.json"]["requirements"].append(child)
    # The existing shared corpus must retain reciprocal requirements.
    graph[1]["fixtures.json"]["fixtures"][0]["requirement_ids"].append(child["id"])
    sync_snapshot(*graph)
    result = validate(graph)
    assert result["required_consumers"] == 38
    assert result["required_contexts"] == 148 + (4 if imgui else 2)
    assert result["verified_contexts"] == 148
    assert any(gap.get("requirement_id") == child["id"] for gap in result["missing_contracts"])
    assert {gap["context"] for gap in result["missing_proofs"] if gap.get("requirement_id") == child["id"]} == execution.required_contexts(child)
    with pytest.raises(ValueError, match="evidence required"):
        validate(graph, strict=True)


def test_multiple_operation_owners_cannot_merge_into_one_erosion_proof(tmp_path):
    graph = owned_consumers(tmp_path)
    row = assignment(graph[1])
    row["requirement_ids"].append("G052.other-dilation-owner")
    sync_snapshot(*graph)
    with pytest.raises(ValueError, match="exactly one owning requirement"):
        validate(graph)


def test_concrete_operation_must_agree_with_owning_interfaces(tmp_path):
    graph = owned_consumers(tmp_path)
    assignment(graph[1])["consumer"]["operation"] = "binary_dilate"
    sync_snapshot(*graph)
    with pytest.raises(ValueError, match="concrete consumer operation"):
        validate(graph)


def test_conversion_owner_rejects_binary_operation_without_name_whitelist(tmp_path):
    graph = owned_consumers(tmp_path)
    child = owner_requirement(graph[1])
    child["interfaces"]["imgui"]["entry_points"] = ["Operation form: convert_image"]
    sync_snapshot(*graph)
    sync_surfaces(*graph)
    with pytest.raises(ValueError, match="concrete consumer operation"):
        validate(graph)


@pytest.mark.parametrize("value", [False, 2.0])
def test_integer_effective_and_override_fields_cannot_be_bool_or_float(tmp_path, value):
    graph = owned_consumers(tmp_path)
    item = contract(graph[1])["contexts"]["cpp_api"]["scenarios"]["override"]
    item.update(effective_parameter_value=value, override_value=value)
    rules = graph[1]["commands.json"]["surface_contracts"][0]["contexts"]["cpp_api"]
    for name in ("effective", "override"):
        position = next(i for i, rule in enumerate(rules) if rule["id"] == item["assertion_ids"][name])
        rules[position] = value_rule(item["assertion_ids"][name], value)
    sync_surfaces(*graph)
    with pytest.raises(ValueError):
        validate(graph)


@pytest.mark.parametrize("scenario", ["frozen", "override", "persisted"])
def test_live_state_cannot_hide_existing_owner_obligations(tmp_path, scenario):
    graph = owned_consumers(tmp_path, persisted=True)
    child = owner_requirement(graph[1])
    child["settings_consumers"] = [{"key": row["key"], "consumer": "Settings snapshot, frozen workflows and preference reset/import/export/reopen"} for row in child["settings_consumers"]]
    row = assignment(graph[1])
    row["consumption_mode"] = "live_state"
    for value in row["contexts"].values():
        value["scenarios"][scenario] = {"applicable": False, "reason": "Owned API is not implemented yet.", "source_refs": deepcopy(row["source_refs"])}
    sync_snapshot(*graph)
    with pytest.raises(ValueError, match="cannot be made inapplicable"):
        validate(graph)


def test_headless_configuration_api_absence_remains_required_gap(tmp_path):
    graph = owned_consumers(tmp_path, persisted=True)
    row = assignment(graph[1])
    row["contexts"]["cpp_api"]["scenarios"]["persisted"] = {"applicable": False, "reason": "Headless C++ file API not implemented.", "source_refs": deepcopy(row["source_refs"])}
    sync_snapshot(*graph)
    with pytest.raises(ValueError, match="cannot be made inapplicable"):
        validate(graph)


def alter_rule(graph, key, scenario, field, expected):
    row = contract(graph[1], key)
    item = row["contexts"]["cpp_api"]["scenarios"][scenario]
    assertion_id = item["assertion_ids"][field]
    rule = next(rule for rule in graph[1]["commands.json"]["surface_contracts"][0]["contexts"]["cpp_api"] if rule["id"] == assertion_id)
    rule["expected"] = expected
    sync_surfaces(*graph)


def test_changed_effect_cannot_equal_default_even_with_passing_observations(tmp_path):
    graph = owned_consumers(tmp_path)
    row = contract(graph[1])
    scenarios = row["contexts"]["cpp_api"]["scenarios"]
    rules = {value["id"]: value for value in graph[1]["commands.json"]["surface_contracts"][0]["contexts"]["cpp_api"]}
    alter_rule(graph, "binary_iterations", "changed", "effect", rules[scenarios["default"]["assertion_ids"]["effect"]]["expected"])
    with pytest.raises(ValueError, match="change setting and computed effect"):
        validate(graph)


@pytest.mark.parametrize("field", ["setting_value", "effective_parameter_value", "current_setting_value"])
def test_frozen_values_must_retain_capture_after_current_setting_mutates(tmp_path, field):
    graph = owned_consumers(tmp_path)
    row = contract(graph[1])
    frozen = row["contexts"]["cpp_api"]["scenarios"]["frozen"]
    frozen[field] = 2 if field != "current_setting_value" else 1
    if field in {"setting_value", "effective_parameter_value"}:
        frozen["setting_value"] = frozen["effective_parameter_value"] = 2
        alter_rule(graph, "binary_iterations", "frozen", "setting", 2)
        alter_rule(graph, "binary_iterations", "frozen", "effective", 2)
    mapping = {"setting_value": "setting", "effective_parameter_value": "effective", "current_setting_value": "current"}
    alter_rule(graph, "binary_iterations", "frozen", mapping[field], frozen[field])
    with pytest.raises(ValueError, match="frozen scenario"):
        validate(graph)


def test_valid_integer_parameter_cannot_substitute_for_consumed_setting(tmp_path):
    graph = owned_consumers(tmp_path)
    row = contract(graph[1])
    scenarios = row["contexts"]["cpp_api"]["scenarios"]
    scenarios["default"]["effective_parameter_value"] = 99
    scenarios["changed"]["effective_parameter_value"] = 98
    scenarios["frozen"]["effective_parameter_value"] = 99
    for scenario, value in (("default", 99), ("changed", 98), ("frozen", 99)):
        alter_rule(graph, "binary_iterations", scenario, "effective", value)
    with pytest.raises(ValueError, match="effective parameter differs from consumed setting"):
        validate(graph)


def converted_memory_graph(tmp_path):
    graph = owned_consumers(tmp_path)
    root, documents, inventory = graph
    owner = owner_requirement(documents)
    parameter = {"name": "history_bytes", "type": "integer", "default": 0, "minimum": 0, "maximum": 1048576 * 1048576, "units": "bytes", "enabled_when": "always"}
    owner["contract"]["parameters"].append(deepcopy(parameter))
    entry = assignment(documents, "history_memory_mb")
    entry["consumer"]["member"] = "history_bytes"
    entry["parameter_schema"] = deepcopy(parameter)
    entry["parameter_conversion"] = {"rule": "scale_offset", "setting_key": "history_memory_mb", "member": "history_bytes", "scale": 1048576, "offset": 0}
    source_path = "src/desktop/window.cpp"
    (root / source_path).parent.mkdir(parents=True, exist_ok=True)
    payload = (ROOT / source_path).read_bytes()
    assert b'history_memory_mb' in payload
    (root / source_path).write_bytes(payload)
    entry["source_refs"].append({"path": source_path, "sha256": digest(payload)})
    row = contract(documents, "history_memory_mb")
    for context in surface.CONTEXTS:
        rules = {a["id"]: a for a in documents["commands.json"]["surface_contracts"][0]["contexts"][context]}
        for scenario, item in row["contexts"][context]["scenarios"].items():
            value = item["effective_parameter_value"] * 1048576
            item["effective_parameter_value"] = value
            rules[item["assertion_ids"]["effective"]]["expected"] = value
            rules[item["assertion_ids"]["effect"]]["expected"] = digest(encoded({"owned_computed_output": "history_memory_mb", "value": value}))
            if scenario == "override":
                item["override_value"] *= 1048576
                rules[item["assertion_ids"]["override"]]["expected"] = item["override_value"]
    sync_snapshot(root, documents, inventory)
    sync_contracts(documents)
    sync_surfaces(root, documents, inventory)
    return graph


def test_source_grounded_mb_to_bytes_conversion_keeps_complete_graph(tmp_path):
    graph = converted_memory_graph(tmp_path)
    item = contract(graph[1], "history_memory_mb")["contexts"]["cpp_api"]["scenarios"]["changed"]
    assert item["setting_value"] == 1
    assert item["effective_parameter_value"] == 1048576
    assert validate(graph, strict=True)["verified_contexts"] == 148


def test_converted_parameter_requires_deterministic_consumption_relation(tmp_path):
    graph = converted_memory_graph(tmp_path)
    del assignment(graph[1], "history_memory_mb")["parameter_conversion"]
    sync_snapshot(*graph)
    with pytest.raises(ValueError, match="both schema and deterministic relation"):
        validate(graph)


def test_unknown_member_cannot_escape_known_parameter_type_validation(tmp_path):
    graph = owned_consumers(tmp_path)
    entry = assignment(graph[1])
    entry["consumer"]["member"] = "unknown_unmapped_iterations"
    entry["parameter_schema"] = {"name": "unknown_unmapped_iterations", "type": "boolean", "default": False, "units": "", "enabled_when": "always"}
    entry["parameter_conversion"] = {"rule": "lookup", "setting_key": "binary_iterations", "member": "unknown_unmapped_iterations", "entries": [{"setting_value": 1, "effective_parameter_value": False}, {"setting_value": 2, "effective_parameter_value": True}]}
    sync_snapshot(*graph)
    with pytest.raises(ValueError, match="unknown consuming member"):
        validate(graph)


def test_converted_native_default_must_match_owner_parameter_default(tmp_path):
    graph = converted_memory_graph(tmp_path)
    entry = assignment(graph[1], "history_memory_mb")
    entry["parameter_conversion"]["offset"] = 1
    sync_snapshot(*graph)
    with pytest.raises(ValueError, match="converted native default"):
        validate(graph)


def converted_choice_graph(tmp_path):
    graph = owned_consumers(tmp_path)
    root, documents, inventory = graph
    parameter = {"name": "unsigned16_bits", "type": "integer", "default": 0, "minimum": 0, "maximum": 16, "choices": [0, 8, 10, 12, 14, 15, 16], "units": "bits", "enabled_when": "always"}
    owner_requirement(documents)["contract"]["parameters"].append(deepcopy(parameter))
    entry = assignment(documents, "conversion_unsigned16_range")
    entry["consumer"]["member"] = "unsigned16_bits"
    entry["parameter_schema"] = deepcopy(parameter)
    entry["parameter_conversion"] = {"rule": "lookup", "setting_key": "conversion_unsigned16_range", "member": "unsigned16_bits", "entries": [{"setting_value": value, "effective_parameter_value": 0 if value == "auto" else int(value)} for value in ("auto", "8", "10", "12", "14", "15", "16")]}
    row = contract(documents, "conversion_unsigned16_range")
    for context in surface.CONTEXTS:
        rules = documents["commands.json"]["surface_contracts"][0]["contexts"][context]
        for scenario, item in row["contexts"][context]["scenarios"].items():
            value = 0 if item["effective_parameter_value"] == "auto" else int(item["effective_parameter_value"])
            item["effective_parameter_value"] = value
            position = next(i for i, rule in enumerate(rules) if rule["id"] == item["assertion_ids"]["effective"])
            rules[position] = value_rule(item["assertion_ids"]["effective"], value)
            effect = next(rule for rule in rules if rule["id"] == item["assertion_ids"]["effect"])
            effect["expected"] = digest(encoded({"owned_computed_output": "conversion_unsigned16_range", "value": value}))
            if scenario == "override":
                item["override_value"] = int(item["override_value"])
                position = next(i for i, rule in enumerate(rules) if rule["id"] == item["assertion_ids"]["override"])
                rules[position] = value_rule(item["assertion_ids"]["override"], item["override_value"])
    sync_snapshot(root, documents, inventory)
    sync_contracts(documents)
    sync_surfaces(root, documents, inventory)
    return graph


def test_typed_choice_lookup_consumes_every_native_choice(tmp_path):
    assert validate(converted_choice_graph(tmp_path), strict=True)["verified_contexts"] == 148


@pytest.mark.parametrize("mutation", ["missing_choice", "duplicate_input", "boolean_output"])
def test_typed_lookup_rejects_missing_duplicate_or_wrong_output_types(tmp_path, mutation):
    graph = converted_choice_graph(tmp_path)
    rows = assignment(graph[1], "conversion_unsigned16_range")["parameter_conversion"]["entries"]
    if mutation == "missing_choice":
        rows.pop()
    elif mutation == "duplicate_input":
        rows.append(deepcopy(rows[0]))
    else:
        rows[0]["effective_parameter_value"] = False
    sync_snapshot(*graph)
    with pytest.raises(ValueError):
        validate(graph)


def unselected_export(graph, *, new_key=False, changed_definition=False):
    root, documents, inventory = graph
    fresh = deepcopy(run(inventory))
    fresh["id"] = "owned-newer-native-export"
    for field in ("surface_traces_schema_version", "surface_traces", "settings_consumer_assignment_snapshot"):
        fresh.pop(field, None)
    value = deepcopy(PINNED_EXPORT)
    if new_key:
        definition = {"name": "owned_future_native_setting", "group": "Owned future", "label": "Owned future", "description": "Owned future exposed setting.", "kind": "int", "default": 1, "minimum": 0.0, "maximum": 100.0, "choices": []}
        value["definitions"].append(definition)
        value["defaults"][definition["name"]] = definition["default"]
    if changed_definition:
        definition = next(row for row in value["definitions"] if row["name"] == "binary_iterations")
        definition["default"] = 2
        value["defaults"]["binary_iterations"] = 2
    output = "build/owned-evidence/newer-settings-schema.json"
    payload = encoded(value)
    (root / output).write_bytes(payload)
    record = fresh["settings_export"]
    identities = record["source_library_executable_output_sha256"]
    identities.pop(record["output"])
    identities[output] = digest(payload)
    record.update(output=output, definitions_compared=len(value["definitions"]), defaults_compared=len(value["defaults"]))
    record["identity_roles"]["output"] = output
    inventory["verification_runs"].append(fresh)
    accepted_binding = deepcopy(documents["commands.json"]["run_bindings"][0])
    accepted_binding["run_id"] = fresh["id"]
    accepted_binding["inventory_run_sha256"] = evidence.run_fingerprint(fresh)
    documents["commands.json"]["run_bindings"].append(accepted_binding)


def test_unselected_new_exported_key_cannot_be_hidden_by_previous_37_bindings(tmp_path):
    graph = owned_consumers(tmp_path)
    unselected_export(graph, new_key=True)
    result = validate(graph)
    assert len(result["exposed_keys"]) == 38
    assert any(gap.get("setting_key") == "owned_future_native_setting" for gap in result["unresolved_enumeration"])
    with pytest.raises(ValueError, match="evidence required"):
        validate(graph, strict=True)


def test_unselected_changed_native_definition_cannot_use_previous_contract(tmp_path):
    graph = owned_consumers(tmp_path)
    unselected_export(graph, changed_definition=True)
    result = validate(graph)
    assert any(gap.get("setting_key") == "binary_iterations" and "latest exposed" in gap["reason"] for gap in result["unresolved_enumeration"])
    with pytest.raises(ValueError, match="evidence required"):
        validate(graph, strict=True)


def test_capture_changed_setting_then_mutate_again_is_supported(tmp_path):
    graph = owned_consumers(tmp_path)
    row = contract(graph[1])
    for context in surface.CONTEXTS:
        scenarios = row["contexts"][context]["scenarios"]
        frozen = scenarios["frozen"]
        frozen.update(captured_scenario="changed", setting_value=2, current_setting_value=3, effective_parameter_value=2)
        scenarios["override"].update(setting_value=2, current_setting_value=2, effective_parameter_value=3, override_value=3)
        rules = {a["id"]: a for a in graph[1]["commands.json"]["surface_contracts"][0]["contexts"][context]}
        for scenario in ("frozen", "override"):
            item = scenarios[scenario]
            for field, value in (("setting", item["setting_value"]), ("current", item["current_setting_value"]), ("effective", item["effective_parameter_value"])):
                rules[item["assertion_ids"][field]]["expected"] = value
            rules[item["assertion_ids"]["effect"]]["expected"] = rules[scenarios["changed"]["assertion_ids"]["effect"]]["expected"] if scenario == "frozen" else digest(encoded({"owned_computed_output": "binary_iterations", "value": 3}))
            if scenario == "override":
                rules[item["assertion_ids"]["override"]]["expected"] = 3
    sync_surfaces(*graph)
    assert validate(graph, strict=True)["verified_contexts"] == 148


def test_bool_override_is_independent_from_frozen_current_mutation(tmp_path):
    graph = owned_consumers(tmp_path)
    row = contract(graph[1], "binary_pad_edges")
    scenarios = row["contexts"]["cpp_api"]["scenarios"]
    assert scenarios["frozen"]["setting_value"] is False
    assert scenarios["frozen"]["current_setting_value"] is True
    assert scenarios["override"]["current_setting_value"] is False
    assert scenarios["override"]["override_value"] is True
    assert validate(graph, strict=True)["verified_contexts"] == 148


def test_override_cannot_merely_read_current_setting(tmp_path):
    graph = owned_consumers(tmp_path)
    item = contract(graph[1])["contexts"]["cpp_api"]["scenarios"]["override"]
    item["current_setting_value"] = item["override_value"]
    alter_rule(graph, "binary_iterations", "override", "current", item["override_value"])
    with pytest.raises(ValueError, match="override scenario"):
        validate(graph)


@pytest.mark.parametrize("mutation", ["boolean", "identity", "readback"])
def test_readback_or_boolean_marker_cannot_qualify_computed_effect(tmp_path, mutation):
    graph = owned_consumers(tmp_path)
    row = contract(graph[1])
    mapping = row["contexts"]["cpp_api"]["scenarios"]["changed"]["assertion_ids"]
    rule = next(rule for rule in graph[1]["commands.json"]["surface_contracts"][0]["contexts"]["cpp_api"] if rule["id"] == mapping["effect"])
    if mutation == "readback":
        mapping["effect"] = mapping["setting"]
    elif mutation == "identity":
        rule["observation"] = "identity"
    else:
        rule.update(rule="scalar_equal", value_type="boolean", expected=True)
    sync_surfaces(*graph)
    with pytest.raises(ValueError):
        validate(graph)


def test_wrong_context_consumer_proof_cannot_borrow_other_launch(tmp_path):
    graph = owned_consumers(tmp_path)
    graph[1]["commands.json"]["settings_consumer_proofs"][0]["surface_proof_id"] = "proof.imgui_python"
    with pytest.raises(ValueError, match="wrong exact surface proof"):
        validate(graph)


def test_consumer_digest_detects_frozen_definition_or_value_changes(tmp_path):
    graph = owned_consumers(tmp_path)
    contract(graph[1])["contexts"]["cpp_api"]["scenarios"]["override"]["override_value"] = 3
    with pytest.raises(ValueError):
        validate(graph)


@pytest.mark.parametrize("kind", ["source", "library", "executable", "export", "snapshot", "report", "trace"])
def test_missing_bytes_are_gaps_and_never_verified(tmp_path, kind):
    graph = owned_consumers(tmp_path)
    paths = {"source": "src/native/settings.cpp", "library": "sdk/lib/imagejxx_core.lib", "executable": "owned/imagejxx_settings_schema_example.exe", "export": EXPORT_PATH, "snapshot": SNAPSHOT, "report": surface.base.PATHS["sdk"], "trace": surface.trace_entry(graph[2], "cpp_api")["path"]}
    (graph[0] / paths[kind]).unlink()
    result = validate(graph)
    assert result["unavailable_evidence"]
    assert result["verified_contexts"] < 148
    with pytest.raises(ValueError, match="evidence required"):
        validate(graph, strict=True)


@pytest.mark.parametrize("kind", ["source", "library", "executable", "export", "snapshot"])
def test_present_stale_bytes_always_reject(tmp_path, kind):
    graph = owned_consumers(tmp_path)
    paths = {"source": "src/native/settings.cpp", "library": "sdk/lib/imagejxx_core.lib", "executable": "owned/imagejxx_settings_schema_example.exe", "export": EXPORT_PATH, "snapshot": SNAPSHOT}
    (graph[0] / paths[kind]).write_bytes(b"changed declared bytes")
    with pytest.raises(ValueError, match="fingerprint changed"):
        validate(graph)


def test_missing_parent_snapshot_does_not_hide_present_bad_source_reference(tmp_path):
    graph = owned_consumers(tmp_path)
    (graph[0] / SNAPSHOT).unlink()
    (graph[0] / "owned/present-bad-source.cpp").write_bytes(b"present actual bytes")
    assignment(graph[1])["source_refs"] = [{"path": "owned/present-bad-source.cpp", "sha256": digest(b"different declared bytes")}]
    with pytest.raises(ValueError, match="consumer_source_reference byte fingerprint"):
        validate(graph)


def test_unused_persistence_contract_does_not_hide_present_bad_file(tmp_path):
    graph = owned_consumers(tmp_path, persisted=True)
    graph[1]["commands.json"]["settings_consumer_proofs"] = []
    item = contract(graph[1])["contexts"]["cpp_api"]["scenarios"]["persisted"]
    path = "build/owned-evidence/unused-bad-preferences.json"
    (graph[0] / path).write_bytes(b"present actual preference file")
    item["persistence"] = {"path": path, "sha256": digest(b"wrong declared preference bytes")}
    with pytest.raises(ValueError, match="persisted_file byte fingerprint"):
        validate(graph)


@pytest.mark.parametrize("raw", [b'{"schema_version":1,"schema_version":1}', b'{"schema_version":1,"unknown":NaN}', b'{"schema_version":1,"unknown":1e999}', '{"schema_version":1}'.encode("utf-16"), b'{"bad":"\xff"}'])
def test_invalid_export_json_rejects_even_with_consistent_byte_hashes(tmp_path, raw):
    graph = owned_consumers(tmp_path)
    (graph[0] / EXPORT_PATH).write_bytes(raw)
    run(graph[2])["settings_export"]["source_library_executable_output_sha256"][EXPORT_PATH] = digest(raw)
    binding(graph[1])["identities"][EXPORT_PATH] = digest(raw)
    binding(graph[1])["export"]["sha256"] = digest(raw)
    rebind(*graph)
    with pytest.raises(ValueError):
        validate(graph)


@pytest.mark.parametrize("mutation", ["missing_mapping", "missing_file_identity", "fabricated_artifact", "wrong_reopened_value"])
def test_persistence_requires_reopen_actual_file_and_effect(tmp_path, mutation):
    graph = owned_consumers(tmp_path, persisted=True)
    row = contract(graph[1])
    item = row["contexts"]["cpp_api"]["scenarios"]["persisted"]
    if mutation == "missing_mapping":
        del item["assertion_ids"]["reopened_setting"]
    elif mutation == "missing_file_identity":
        del item["persistence"]
    elif mutation == "fabricated_artifact":
        entry = surface.trace_entry(graph[2], "cpp_api")
        entry["producer"]["artifacts"].remove(item["persistence"])
        rebind(*graph)
    else:
        alter_rule(graph, "binary_iterations", "persisted", "reopened_setting", 1)
    row["sha256"] = consumer.contract_fingerprint(row)
    with pytest.raises(ValueError):
        validate(graph)


@pytest.mark.parametrize("kind",["source","library","executable","export","snapshot","report","trace"])
def test_unreadable_consumer_evidence_has_no_credit_and_strict_rejection(tmp_path,monkeypatch,kind):
    graph=owned_consumers(tmp_path)
    paths={"source":"src/native/settings.cpp","library":"sdk/lib/imagejxx_core.lib","executable":"owned/imagejxx_settings_schema_example.exe","export":EXPORT_PATH,"snapshot":SNAPSHOT,"report":surface.base.PATHS["sdk"],"trace":surface.trace_entry(graph[2],"cpp_api")["path"]}
    target=graph[0]/paths[kind];original=Path.read_bytes
    def denied(self,*args,**kwargs):
        if self==target:raise PermissionError("owned denied consumer evidence")
        return original(self,*args,**kwargs)
    monkeypatch.setattr(Path,"read_bytes",denied)
    result=validate(graph)
    assert result["unavailable_evidence"] and result["verified_contexts"]<148
    with pytest.raises(ValueError,match="evidence required"):validate(graph,strict=True)
