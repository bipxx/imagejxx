"""Independent, product-free comparisons for the six saved-ROI surface contracts.

The 24 cases compare genuine scalar extractions with the adopted frozen rules.
They preserve SDK/host and old paired/supplement origins separately. Successful
comparison emits a trace; it does not adopt evidence or claim whole-family,
commercial, OS/GPU, performance or distribution acceptance.
"""

from __future__ import annotations

from copy import deepcopy
import hashlib
import importlib.util
import itertools
import json
import os
from pathlib import Path
import re
import sys

import pytest


ROOT = Path(__file__).resolve().parents[2]
STEMS = ("geometry", "targets", "publication", "resources", "record-replay-rebind",
         "persistence-surfaces")
CONTEXTS = ("cpp_api", "python_api", "imgui_cpp", "imgui_python")
PARAMETERS = tuple(itertools.product(STEMS, CONTEXTS))
PARAMETER_IDS = tuple(stem + "-" + context for stem, context in PARAMETERS)
EXPECTED_COLLECTION_IDS = tuple("test_saved_roi_geometry_current_surface[" + name + "]"
                                for name in PARAMETER_IDS)
CONTRACT_HASHES = {
    "geometry": "2daaf6fab50688765088d4be9d62bfcb3d0370e7a43bda6d4b502759a12b6004",
    "targets": "3d37c167a45b58cbabb9568ab24714f971df27c1b390bbcedac9f294455c69ab",
    "publication": "ef79b7a8eada582413dc11b47ca3a2d533de1182be79f4ce7c1382f4af4ded0b",
    "resources": "f1473f98058e1d3210dcb14c03c0a5581a320ad7d41df20cc6d05f84d03eee28",
    "record-replay-rebind": "6db731b9fddb0bc2c3e84e6d07eaea7753c65b0d2bbbbf0d5b4d19faaa60336d",
    "persistence-surfaces": "48035e1d846e4a6ca9c61f105af7079b34636af23a0a6307b7194c55ff0deae0",
}
RAW_IDS = {"sdk": "p06g.saved-roi.sdk.raw.001", "host": "p06g.saved-roi.host.raw.001",
           "python": "p06g.saved-roi.python.raw.001"}
MAX_FILE_BYTES = 32 * 1024 * 1024
# Includes both exact input validation and the final mutation recheck. The
# already retained UI pin tree alone declares about125MB of distinct inputs.
MAX_TOTAL_BYTES = 384 * 1024 * 1024
MAX_OUTPUT_BYTES = 16 * 1024 * 1024
SCOPE_LIMITS = [
    "Exactly six frozen contracts, 49 C++/49 Python/51 per shared-ImGui launch; no complete option or family acceptance.",
    "C++ is SDK47 plus host2 with distinct original producers, source cases and artifacts; not standalone SDK49.",
    "UI supplements are labelled shared-core API dependencies or actual Numeric-camera UI work; APIs are not widgets.",
    "Original paired20 and later supplement launch origins remain separate; no recapture or original-path relabelling.",
    "No OS/GPU, performance, commercial-distribution or release acceptance is implied.",
]


def _no_products():
    assert not any(name == "imagejxx" or name.startswith("imagejxx.")
                   for name in sys.modules), "reader must not import a product"


def _path(value):
    assert isinstance(value, str) and value and "\x00" not in value
    member = Path(value)
    if not member.is_absolute():
        member = ROOT / member
    # Inputs are explicit file pins. Never enumerate parents or discover paths.
    assert member.is_relative_to(ROOT), "pinned evidence must remain in workspace"
    assert ".." not in member.parts
    for node in (member, *member.parents):
        assert not node.is_symlink() and not getattr(node, "is_junction", lambda: False)(), "aliased evidence path"
        if node == ROOT:
            break
    return member


def _identity(pin):
    assert isinstance(pin, dict)
    assert isinstance(pin.get("path"), str)
    assert type(pin.get("bytes")) is int and 0 <= pin["bytes"] <= MAX_FILE_BYTES
    assert isinstance(pin.get("sha256"), str) and re.fullmatch(r"[0-9a-f]{64}", pin["sha256"])
    return str(_path(pin["path"])), pin["bytes"], pin["sha256"]


def _source_identity(pin):
    return _identity({**pin, "path": pin.get("logical_path", pin["path"])})


def _strict(data):
    def pairs(items):
        result = {}
        for key, value in items:
            assert key not in result, "duplicate JSON key"
            result[key] = value
        return result

    def constant(value):
        raise AssertionError("nonfinite JSON value: " + value)

    return json.loads(data.decode("utf-8-sig"), object_pairs_hook=pairs, parse_constant=constant)


class SavedRoiEvidence:
    def __init__(self, config_path, output):
        _no_products()
        self.read_bytes = 0
        self.read_pins = {}
        self.external_pins = {}
        self.snapshots = {}
        self.source_versions = {}
        self.output_bytes = 0
        self.output = output
        assert not output.exists(), "comparison output directory must be fresh"
        self.config_path = config_path
        data = self._read_file(config_path, MAX_FILE_BYTES)
        self.config_pin = self._pin(config_path, data)
        self.config = _strict(data)
        m = self.config
        assert set(m) == {"schema_version", "kind", "run_id", "contracts", "reader_source",
                          "schema_helper", "source_freeze", "source_versions", "extractions", "receipts", "raw_producers"}
        assert type(m["schema_version"]) is int and m["schema_version"] == 1
        assert m["kind"] == "p06g.saved-roi.current-surface-inputs.001"
        assert isinstance(m["run_id"], str) and re.fullmatch(r"[A-Za-z0-9_.-]+", m["run_id"])
        freeze = self.read(m["source_freeze"])
        assert set(freeze) == {"schema_version", "sources", "source_snapshot"}
        assert freeze["schema_version"] == 1 and type(freeze["schema_version"]) is int
        assert isinstance(freeze["sources"], dict)
        snapshot_root = _path(freeze["source_snapshot"])
        for logical, digest in freeze["sources"].items():
            assert re.fullmatch(r"[0-9a-f]{64}", digest)
            original = _path(logical)
            snapshot = snapshot_root / logical
            assert snapshot.is_relative_to(snapshot_root)
            self.snapshots[str(original)] = (snapshot, digest)
        for source in m["source_versions"]:
            assert "logical_path" in source
            original = str(_path(source["logical_path"]))
            key = original, source["sha256"]
            assert key not in self.source_versions
            self.source_versions[key] = _path(source["path"])
        for source in m["source_versions"]:
            self.verify(source)
        self.verify(m["reader_source"])
        assert _path(m["reader_source"]["path"]) == Path(__file__).resolve()
        assert hashlib.sha256(Path(__file__).read_bytes()).hexdigest() == m["reader_source"]["sha256"]
        helper = m["schema_helper"]
        self.verify(helper)
        helper_path = self.actual_path(helper)
        for name in ("parity_evidence.py", "parity_schema.py"):
            logical = str(_path(str(Path(helper["path"]).with_name(name))))
            assert logical in self.snapshots, "schema companion must be frozen"
            member, digest = self.snapshots[logical]
            self.verify({"path": str(Path(helper["path"]).with_name(name)),
                         "bytes": member.stat().st_size, "sha256": digest})
        spec = importlib.util.spec_from_file_location("_p06g_saved_roi_surface_schema", helper_path)
        assert spec is not None and spec.loader is not None
        self.schema = importlib.util.module_from_spec(spec)
        previous = sys.dont_write_bytecode
        try:
            sys.dont_write_bytecode = True
            spec.loader.exec_module(self.schema)
        finally:
            sys.dont_write_bytecode = previous
        contracts = self.read(m["contracts"])
        assert set(contracts) == {"schema_version", "contracts"} and contracts["schema_version"] == 1
        self.contracts = {}
        for contract in contracts["contracts"]:
            stem = contract["requirement_id"].removeprefix("G015.saved-roi-geometry.")
            assert stem in STEMS and stem not in self.contracts
            assert contract["id"] == "SC.G015.saved-roi-geometry." + stem
            assert set(contract["contexts"]) == set(CONTEXTS)
            assert contract["sha256"] == CONTRACT_HASHES[stem] == self.schema.contract_fingerprint(contract)
            self.contracts[stem] = contract
        assert set(self.contracts) == set(STEMS)
        assert [sum(len(c["contexts"][context]) for c in self.contracts.values())
                for context in CONTEXTS] == [49, 49, 51, 51]
        assert set(m["receipts"]) == {"api_qualification", "api_extraction", "paired_capture",
                                      "paired_reader", "ui_capture", "ui_extraction", "ui_source_compatibility"}
        self.receipts = {key: self.read(pin) for key, pin in m["receipts"].items()}
        assert all(receipt.get("state") == "passed" for receipt in self.receipts.values())
        assert self.receipts["api_qualification"]["failures"] == []
        assert self.receipts["paired_reader"]["capture"] == m["receipts"]["paired_capture"]
        assert self.receipts["ui_capture"]["original_capture"] == m["receipts"]["paired_capture"]
        assert set(m["extractions"]) == {"api", "ui"}
        self.extractions = {key: self.read(pin) for key, pin in m["extractions"].items()}
        assert self.receipts["api_extraction"]["positive_extraction"] == m["extractions"]["api"]
        assert _identity(self.receipts["ui_extraction"]["positive_extraction"]) == _identity(m["extractions"]["ui"])
        self.rows = {}
        self.raw_values = {}
        self.ui_inputs = {}
        self.ui_input_origins = {}
        self.ui_unsupported = {}
        self._producers()
        self._api()
        self._ui()
        assert set(self.rows) == set(CONTEXTS)
        output.mkdir(parents=True, exist_ok=False)

    def _read_file(self, path, limit):
        size = path.stat().st_size
        assert 0 <= size <= limit
        with path.open("rb") as stream:
            data = stream.read(limit + 1)
        assert len(data) == size <= limit
        self.read_bytes += len(data)
        assert self.read_bytes <= MAX_TOTAL_BYTES
        return data

    @staticmethod
    def _pin(path, data):
        return {"path": path.relative_to(ROOT).as_posix(), "bytes": len(data),
                "sha256": hashlib.sha256(data).hexdigest()}

    def actual_path(self, pin):
        original, _, digest = _identity(pin)
        if (original, digest) in self.source_versions:
            return self.source_versions[original, digest]
        if original in self.snapshots and self.snapshots[original][1] == digest:
            member, expected = self.snapshots[original]
            assert digest == expected
            return _path(str(member))
        return _path(pin["path"])

    def verify(self, pin):
        identity = _identity(pin)
        if identity not in self.read_pins:
            data = self._read_file(self.actual_path(pin), MAX_FILE_BYTES)
            assert len(data) == identity[1] and hashlib.sha256(data).hexdigest() == identity[2], "pinned bytes changed"
            self.read_pins[identity] = (deepcopy(pin), data)
        return self.read_pins[identity][1]

    def read(self, pin):
        return _strict(self.verify(pin))

    def _producers(self):
        declared = self.config["raw_producers"]
        assert set(declared) == {"sdk", "host", "python", "imgui_cpp", "imgui_python"}
        for role, value in declared.items():
            producers = value if role.startswith("imgui_") else [value]
            assert isinstance(producers, list) and len(producers) == (2 if role.startswith("imgui_") else 1)
            for producer in producers:
                self.schema.producer_identity(producer, role)
                expected_context = "cpp_api" if role in ("sdk", "host") else "python_api" if role == "python" else role
                assert producer["context"] == expected_context
                if role in RAW_IDS:
                    assert producer["id"] == RAW_IDS[role]
                for pin in producer["sources"] + producer["artifacts"]:
                    self.verify(pin)

    def _api(self):
        extraction = self.extractions["api"]
        assert extraction["schema_version"] == 1 and type(extraction["schema_version"]) is int
        assert extraction["kind"] == "p06g.saved-roi-api-surface-extractions.001"
        assert extraction["acceptance_claim"] is False and extraction["complete"] is True
        assert set(extraction["contexts"]) == {"cpp_api", "python_api"}
        qualification = self.receipts["api_qualification"]
        raw_cpp = {row["producer_id"]: row for row in qualification["raw_cpp"]}
        assert set(raw_cpp) == {RAW_IDS["sdk"], RAW_IDS["host"]}
        for role, field in (("sdk", "cpp_sdk"), ("host", "cpp_host")):
            assert _identity(raw_cpp[RAW_IDS[role]]["input"]) == _identity(extraction["inputs"][field])
        assert {_identity(row["input"]) for row in qualification["raw_python"]} == {
            _identity(pin) for pin in extraction["inputs"]["python_raw"]}
        assert [raw_cpp[RAW_IDS[role]]["observation_count"] for role in ("sdk", "host")] == [47, 2]
        assert sum(row["observation_count"] for row in qualification["raw_python"]) == 49
        self.verify(extraction["inputs"]["extractor"])
        baseline = self.read(qualification["source_freeze"])
        baseline_sources = {pin["path"]: pin for pin in baseline["sources"]}
        for role in RAW_IDS:
            configured = self.config["raw_producers"][role]
            for pin in configured["sources"]:
                logical = pin.get("logical_path", pin["path"])
                assert _source_identity(pin) == _identity(baseline_sources[logical])
        product_pins = qualification["products"]
        for role, filename in (("sdk", "imagejxx_saved_roi_geometry_example.exe"),
                               ("host", "test_saved_roi_geometry_host.exe")):
            product = [pin for pin in product_pins if Path(pin["path"]).name == filename]
            assert len(product) == 1 and _identity(product[0]) in {
                _identity(pin) for pin in self.config["raw_producers"][role]["artifacts"]}
        origin_pins = [pin for pin in qualification["artifacts"]
                       if Path(pin["path"]).name in ("python-origins-before.json", "python-origins-after.json")]
        assert len(origin_pins) == 2
        origins = [self.read(pin) for pin in origin_pins]
        assert self.schema.same(origins[0], origins[1])
        assert {_identity(pin) for pin in origins[0]["modules"].values()} <= {
            _identity(pin) for pin in self.config["raw_producers"]["python"]["artifacts"]}
        self._pin_tree(origins[0])
        expected_producers = {RAW_IDS[role]: role for role in RAW_IDS}
        assert len(extraction["producers"]) == 3 and {p["id"] for p in extraction["producers"]} == set(expected_producers)
        for producer in extraction["producers"]:
            role = expected_producers[producer["id"]]
            assert producer["context"] == self.config["raw_producers"][role]["context"]
            membership = qualification["exact_membership"]["python_api" if role == "python" else role]
            assert [case["id"] for case in producer["cases"]] == membership
            ids = set()
            for case in producer["cases"]:
                assert case["id"] not in ids
                ids.add(case["id"])
                raw = self.read(case["raw_input"])
                assert raw["producer_id"] == producer["id"] and raw["complete"] is True
                assert type(raw["schema_version"]) is int and raw["schema_version"] == 1
                assert len(raw["cases"]) == (1 if role == "python" else len(membership))
                source_cases = [row for row in raw["cases"] if row["id"] == case["id"]]
                assert len(source_cases) == 1
                observations = source_cases[0]["observations"]
                assert [row["id"] for row in observations] == case["observation_ids"]
                assert len({row["id"] for row in observations}) == len(observations)
                for row in observations:
                    assert set(row) == {"id", "encoding", "value"} and row["encoding"] == "scalar"
                    key = producer["id"], case["id"], row["id"]
                    assert key not in self.raw_values
                    self.raw_values[key] = (row["value"], case["raw_input"])
            assert len(ids) == {"sdk": 5, "host": 8, "python": 12}[role]
        for context, rows in extraction["contexts"].items():
            for row in rows:
                for assertion in row["assertions"]:
                    source = assertion["source"]
                    key = source["producer_id"], source["case_id"], source["observation_id"]
                    actual, raw_pin = self.raw_values[key]
                    assert source["observation_id"] == assertion["id"]
                    assert _identity(source["raw_input"]) == _identity(raw_pin)
                    assert self.schema.same(actual, assertion["observed"]), "extraction differs from original actual scalar"
            self.rows[context] = rows

    def _pin_tree(self, value):
        if isinstance(value, dict):
            if {"path", "bytes", "sha256"} <= set(value):
                path = Path(value["path"])
                if path.is_absolute() and not path.is_relative_to(ROOT):
                    # An origin, not a formal producer artifact. This is the
                    # already qualified actual CPython engine, not a venv shim.
                    assert path == Path("C:/Users/HEQ/AppData/Roaming/uv/python/cpython-3.12.8-windows-x86_64-none/python.exe")
                    assert value["bytes"] == 91648
                    assert value["sha256"] == "71b986e874ae1382c434f95852f5670e6d87576db2861cd03737dd4bce9d69ed"
                    if str(path) not in self.external_pins:
                        data = self._read_file(path, 1024 * 1024)
                        assert len(data) == value["bytes"] and hashlib.sha256(data).hexdigest() == value["sha256"]
                        self.external_pins[str(path)] = deepcopy(value)
                else:
                    self.verify(value)
            else:
                for child in value.values():
                    self._pin_tree(child)
        elif isinstance(value, list):
            for child in value:
                self._pin_tree(child)

    def _ui(self):
        # The separately owned extractor must preserve original raw input and
        # checkpoint provenance for all102 actual values, including supplements.
        extraction = self.extractions["ui"]
        assert extraction["schema_version"] == 1 and type(extraction["schema_version"]) is int
        assert extraction["kind"] == "p06g.saved-roi-ui-surface-extractions.001"
        assert extraction["acceptance_claim"] is False and extraction["complete"] is True
        assert set(extraction["contexts"]) == {"imgui_cpp", "imgui_python"}
        self._pin_tree(extraction["inputs"])
        self.ui_raw = {}
        declared_ids = set()
        for producer in extraction["producers"]:
            context = producer["context"]
            assert context in CONTEXTS[2:]
            assert producer["id"] == producer["contract"] + "." + context
            assert producer["id"] not in declared_ids
            declared_ids.add(producer["id"])
            assert producer["contract"] in {"p06g.saved-roi-geometry-ui.001",
                                             "p06g.saved-roi-geometry-ui-dependencies.001"}
            assert isinstance(producer["id_basis"], str) and producer["id_basis"]
            configured = [row for row in self.config["raw_producers"][context] if row["id"] == producer["id"]]
            assert len(configured) == 1
            for kind in ("sources", "artifacts"):
                identity = _source_identity if kind == "sources" else _identity
                assert {identity(pin) for pin in configured[0][kind]} == {identity(pin) for pin in producer[kind]}
            for pin in producer["sources"] + producer["artifacts"]:
                self.verify(pin)
            expected_cases = ["PX%02d" % index for index in range(1, 11)] if producer["contract"] == (
                "p06g.saved-roi-geometry-ui.001") else ["api-dependencies-and-numeric-camera"]
            assert [case["id"] for case in producer["cases"]] == expected_cases
            for case in producer["cases"]:
                raw = self.read(case["raw_input"])
                key = producer["id"], case["id"]
                assert key not in self.ui_raw
                assert raw["contract"] == producer["contract"] and raw["context"] == context
                assert raw["case_id"] == case["id"] and raw["observation_state"] == "observed"
                assert raw["acceptance_claim"] is False and raw.get("window_error", "") == ""
                self.ui_raw[key] = (raw, case["raw_input"])
        assert declared_ids == {contract + "." + context for context in CONTEXTS[2:] for contract in (
            "p06g.saved-roi-geometry-ui.001", "p06g.saved-roi-geometry-ui-dependencies.001")}
        assert set(extraction["input_traces"]) == set(CONTEXTS[2:])
        self.input_traces = extraction["input_traces"]
        for context, traces in self.input_traces.items():
            trace_keys = [(trace["producer_id"], trace["case_id"]) for trace in traces]
            assert len(trace_keys) == len(set(trace_keys)) == 11
            assert set(trace_keys) == {key for key, (raw, _) in self.ui_raw.items() if raw["context"] == context}
            for trace in traces:
                key = trace["producer_id"], trace["case_id"]
                raw, pin = self.ui_raw[key]
                assert _identity(pin) == _identity(trace["raw_input"])
                assert raw["context"] == context
                actual_frames = [{"ordinal": frame["ordinal"], "label": frame["label"],
                                  "events": frame["requested_events"]} for frame in raw["frames"]
                                 if frame["requested_events"]]
                assert self.schema.same(trace["frames"], actual_frames), "input trace differs from actual raw frames"
        for context, rows in extraction["contexts"].items():
            for row in rows:
                for assertion in row["assertions"]:
                    source = assertion["source"]
                    raw, pin = self.ui_raw[source["producer_id"], source["case_id"]]
                    assert source["context"] == context == raw["context"]
                    assert source["contract"] == raw["contract"]
                    assert _identity(source["raw_input"]) == _identity(pin)
                    assert isinstance(source["paths"], list) and source["paths"]
                    assert isinstance(source["derivation"], str) and source["derivation"]
                    assert isinstance(source["evidence_scope"], str) and source["evidence_scope"]
            self.rows[context] = rows

    def concrete_inputs(self, context, origins):
        keys = {(origin["source"]["producer_id"], origin["source"]["case_id"]) for origin in origins}
        inputs, provenance, unsupported = [], [], []
        for trace in self.input_traces[context]:
            if (trace["producer_id"], trace["case_id"]) not in keys:
                continue
            position = None
            buttons = {"left": False, "middle": False, "right": False}
            for frame in trace["frames"]:
                for index, event in enumerate(frame["events"]):
                    actual = None
                    kind = event["type"]
                    if kind == "position":
                        assert event["space"] == "screen" and len(event["point"]) == 2
                        position = deepcopy(event["point"])
                        held = [name for name, down in buttons.items() if down]
                        assert len(held) <= 1
                        name = held[0] if held else "left"
                        actual = {"type": "pointer", "position": position, "button": name, "down": buttons[name]}
                    elif kind == "button":
                        assert position is not None and event["name"] in buttons and type(event["down"]) is bool
                        buttons[event["name"]] = event["down"]
                        actual = {"type": "pointer", "position": deepcopy(position),
                                  "button": event["name"], "down": event["down"]}
                    elif kind == "key":
                        actual = {"type": "key", "key": event["name"], "down": event["down"]}
                    elif kind == "keys":
                        assert isinstance(event["keys"], list) and event["keys"]
                        for subindex, key in enumerate(event["keys"]):
                            assert set(key) == {"name", "down"}
                            inputs.append({"type": "key", "key": key["name"], "down": key["down"]})
                            provenance.append({"input_index": len(inputs) - 1, "producer_id": trace["producer_id"],
                                               "case_id": trace["case_id"], "raw_input": trace["raw_input"],
                                               "frame": frame["ordinal"], "label": frame["label"],
                                               "event_index": index, "subevent_index": subindex,
                                               "actual_requested_event": deepcopy(event)})
                    elif kind == "text":
                        actual = {"type": "text", "text": event["value"]}
                    elif kind == "wheel":
                        unsupported.append({"producer_id": trace["producer_id"], "case_id": trace["case_id"],
                                            "frame": frame["ordinal"], "event_index": index, "event": deepcopy(event),
                                            "reason": "generic surface input schema has no wheel descriptor"})
                    else:
                        raise AssertionError("unknown actual input event: " + kind)
                    if actual is not None:
                        inputs.append(actual)
                        provenance.append({"input_index": len(inputs) - 1, "producer_id": trace["producer_id"],
                                           "case_id": trace["case_id"], "raw_input": trace["raw_input"],
                                           "frame": frame["ordinal"], "label": frame["label"], "event_index": index,
                                           "actual_requested_event": deepcopy(event)})
            assert not any(buttons.values()), "retained actual mouse press"
        return inputs, provenance, unsupported

    def compare(self, stem, context):
        contract = self.contracts[stem]
        rows = self.rows[context]
        assert len(rows) == len(STEMS)
        assert len({row["contract_id"] for row in rows}) == len(STEMS)
        candidates = [row for row in rows if row["contract_id"] == contract["id"]]
        assert len(candidates) == 1
        row = candidates[0]
        assert row["requirement_id"] == contract["requirement_id"]
        assert row["contract_sha256"] == contract["sha256"]
        frozen = {a["id"]: a for a in contract["contexts"][context]}
        assertions = row["assertions"]
        assert len(assertions) == len(frozen) and {a["id"] for a in assertions} == set(frozen)
        actual_assertions = []
        origins = []
        for actual in assertions:
            expected = frozen[actual["id"]]
            # Expected values originate only in the adopted frozen contract.
            # Actual values originate only in the separately pinned extraction.
            self.schema.assertion_rule(actual, actual["id"], observed=True)
            for key in ("rule", "value_type", "expected", "observation", "ui_state"):
                assert (key in actual) == (key in expected)
                if key in expected:
                    assert self.schema.same(actual[key], expected[key])
            assert "source" in actual, "every actual needs original carrier provenance"
            origins.append({"assertion_id": actual["id"], "source": deepcopy(actual["source"])})
            actual_assertions.append({**deepcopy(expected), "observed": deepcopy(actual["observed"])})
        inputs, input_origins, unsupported = [], [], []
        if context.startswith("imgui_"):
            inputs, input_origins, unsupported = self.concrete_inputs(context, origins)
            self.schema.input_events(inputs, context)
        self.emit(stem, context, actual_assertions, origins, inputs, input_origins, unsupported)

    def write(self, path, value):
        data = self.schema.canonical(value) + b"\n"
        self.output_bytes += len(data)
        assert self.output_bytes <= MAX_OUTPUT_BYTES
        with path.open("xb") as stream:
            assert stream.write(data) == len(data)
        return self._pin(path, data)

    def emit(self, stem, context, assertions, origins, inputs, input_origins, unsupported):
        keys = ("sdk", "host") if context == "cpp_api" else ("python",) if context == "python_api" else (context,)
        dependencies = []
        for key in keys:
            value = self.config["raw_producers"][key]
            dependencies.extend(value if isinstance(value, list) else [value])
        sources = [self.config["reader_source"], self.config["schema_helper"]]
        artifacts = [self.config["extractions"]["api" if context.endswith("_api") else "ui"]]
        for producer in dependencies:
            sources.extend(producer["sources"])
            artifacts.extend(producer["artifacts"])
        def unique(pins):
            result = {}
            for pin in pins:
                key = pin["path"]
                assert key not in result or self.schema.same(result[key], pin)
                result[key] = deepcopy(pin)
            return list(result.values())
        sources, artifacts = unique(sources), unique(artifacts)
        assert not ({pin["path"] for pin in sources} & {pin["path"] for pin in artifacts})
        producer = {"id": "p06g.saved-roi.current.surface." + context + ".001",
                    "context": context, "sources": sources, "artifacts": artifacts}
        report = {"suite": "p06g_saved_roi_current_surfaces",
                  "classname": "tests.qualification.test_saved_roi_geometry_surface_reader",
                  "name": "test_saved_roi_geometry_current_surface[" + stem + "-" + context + "]"}
        contract = self.contracts[stem]
        case_id = "case.saved-roi.current." + stem + "." + context
        case = {"id": case_id, "requirement_id": contract["requirement_id"], "context": context,
                "contract_id": contract["id"], "contract_sha256": contract["sha256"],
                "report_role": "python", "report_case_identity": report, "assertions": assertions}
        if context.startswith("imgui_"):
            case["inputs"] = deepcopy(inputs)
        trace = {"schema_version": 1, "run_id": self.config["run_id"], "producer_id": producer["id"],
                 "acceptance_claim": False, "complete_requirement_coverage": False, "cases": [case],
                 "raw_producers": deepcopy(dependencies), "observation_origins": origins,
                 "input_manifest": self.config_pin, "scope_limits": SCOPE_LIMITS,
                 "input_provenance": input_origins, "unsupported_inputs": unsupported}
        pin = self.write(self.output / (stem + "-" + context + ".trace.json"), trace)
        catalogue = {"id": "trace.saved-roi.current." + stem + "." + context, "path": pin["path"],
                     "sha256": pin["sha256"], "trace_case_id": case_id, "report_role": "python",
                     "report_case_identity": report, "producer": producer}
        self.write(self.output / (stem + "-" + context + ".catalogue.json"),
                   {"schema_version": 1, "run_id": self.config["run_id"], "acceptance_claim": False,
                    "complete_requirement_coverage": False, "trace": catalogue,
                    "raw_producers": deepcopy(dependencies)})

    def recheck(self):
        for pin, _ in self.read_pins.values():
            data = self._read_file(self.actual_path(pin), MAX_FILE_BYTES)
            assert len(data) == pin["bytes"] and hashlib.sha256(data).hexdigest() == pin["sha256"]
        data = self._read_file(self.config_path, MAX_FILE_BYTES)
        assert self._pin(self.config_path, data) == self.config_pin
        for path, pin in self.external_pins.items():
            data = self._read_file(Path(path), 1024 * 1024)
            assert len(data) == pin["bytes"] and hashlib.sha256(data).hexdigest() == pin["sha256"]
        _no_products()


@pytest.fixture(scope="module")
def saved_roi_current_evidence():
    inputs = os.environ.get("IMAGEJXX_SAVED_ROI_SURFACE_INPUTS")
    output = os.environ.get("IMAGEJXX_SAVED_ROI_SURFACE_OUTPUT_DIR")
    assert inputs and output, "both saved-ROI comparison paths are required; no skip or fallback"
    evidence = SavedRoiEvidence(_path(inputs), _path(output))
    yield evidence
    evidence.recheck()


@pytest.mark.parametrize("stem,context", PARAMETERS, ids=PARAMETER_IDS)
def test_saved_roi_geometry_current_surface(stem, context, saved_roi_current_evidence):
    saved_roi_current_evidence.compare(stem, context)
