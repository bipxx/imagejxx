"""Current retained rank comparisons: six scoped contracts, four contexts.

These 24 comparisons retain exactly the adopted 196 assertion identities. SDK
reservation, host virtual-provider and paired R13 observations remain separate;
neither their counts nor this scoped comparison close any rank requirement.
No product module, kernel, Java process or desktop is called by this reader.
"""
import hashlib
import importlib.util
import itertools
import os
from pathlib import Path
import re
import sys
import zipfile
import zlib

import pytest


ROOT = Path(__file__).resolve().parents[2]
SHARED_PATH = ROOT / "tests/qualification/test_rank_surface_reader.py"
SHARED_SHA256 = "10a4f068c41f3ff0dfd0a4afa1bb4c9bb9e6da53e989cd1efe7f1796f40de43e"
STEMS = ("profile-method-radius", "targets-roi", "preview-publication",
         "lifecycle-resources", "record-replay-rebind", "persistence-surfaces")
CONTEXTS = ("cpp_api", "python_api", "imgui_cpp", "imgui_python")
PARAMETERS = tuple(itertools.product(STEMS, CONTEXTS))
PARAMETER_IDS = tuple(stem + "-" + context for stem, context in PARAMETERS)
EXPECTED_COLLECTION_IDS = tuple("test_rank_current_surface[" + name + "]" for name in PARAMETER_IDS)
CURRENT_CASE_IDS = tuple("R%02d" % index for index in range(1, 14))
HOST_ADOPTED_IDS = {"host.changed.workflow", "host.noop.redo", "host.noop.workflow", "host.rank.parameter_keys"}
HOST_EXTRA_IDS = {"host.virtual." + name for name in ("refusal_error", "reads_before", "reads_after",
                  "native_member_after", "frontend_count", "active_owner", "reserved_bytes", "retained_bytes")}
SDK_EXTRA_IDS = {"sdk.reservation." + state + "." + field
                 for state in ("queued_canceled", "ready", "running", "terminal")
                 for field in ("acquired", "declared_bytes", "peak_reserved_bytes")}


def _load_pure_reader(path, expected_sha, name):
    data = path.read_bytes()
    assert hashlib.sha256(data).hexdigest() == expected_sha, "pure extraction source identity changed"
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


# Reuse the unchanged typed pixel/actual-input extraction and assertion logic.
# The historical constructor, source admission and archive loader are NOT called.
shared = _load_pure_reader(SHARED_PATH, SHARED_SHA256, "_rank_historical_pure_extractions")
_path = shared._path
_canonical = shared._canonical
_sha = shared._sha
_pin_identity = shared._pin_identity
_unique_pins = shared._unique_pins


def _same_pin(left, right):
    return _pin_identity(left) == _pin_identity(right)


def _no_product_imports():
    assert not any(name == "imagejxx" or name.startswith("imagejxx.") for name in sys.modules)


class CurrentRankEvidence(shared.RankEvidence):
    def __init__(self, manifest_path, output):
        _no_product_imports()
        self.verified, self.admitted_pins, self.archived = set(), {}, {}
        self.raw = {key: {} for key in shared.RAW_PRODUCERS}
        self.ui_rows, self.ui_values = {}, {}
        self.output, self.written_bytes = output, 0
        assert not output.exists() or (output.is_dir() and not any(output.iterdir())), "fresh empty output required"
        output.mkdir(parents=True, exist_ok=True)
        self.manifest_path = manifest_path
        self.manifest = self.read_path(manifest_path)
        m = self.manifest
        required = {"schema_version", "kind", "run_id", "adoption", "proposal", "reader_source",
                    "extraction_source", "paired_reader_source", "receipts", "raw", "lineage", "producers"}
        assert required <= set(m) <= required | {"scope"}
        assert type(m["schema_version"]) is int and m["schema_version"] == 1
        assert m["kind"] == "p06f.rank-current-surface-inputs.001"
        assert isinstance(m["run_id"], str) and re.fullmatch(r"[A-Za-z0-9_.-]+", m["run_id"])
        assert m["run_id"] != "p06f.rank-surface-comparison.001", "historical run cannot be relabelled"
        assert set(m["receipts"]) == {"sdk", "host", "python", "paired", "paired_reader"}
        assert set(m["raw"]) == {"sdk", "host"}
        assert set(m["producers"]) == {"sdk", "host", "python", "imgui_cpp", "imgui_python"}
        assert set(m["lineage"]) == {"sdk_inputs", "host_inputs", "installation", "wheel_build", "wheel_inputs"}
        self.manifest_pin = self.local_pin(manifest_path)
        self.verify(m["reader_source"])
        assert _path(m["reader_source"]["path"]) == Path(__file__).resolve()
        self.verify(m["extraction_source"])
        assert _path(m["extraction_source"]["path"]) == SHARED_PATH
        assert m["extraction_source"]["sha256"] == SHARED_SHA256
        self.adoption, self.proposal = self.read_pin(m["adoption"]), self.read_pin(m["proposal"])
        self._contracts()
        self.receipts = {key: self.read_pin(pin) for key, pin in m["receipts"].items()}
        self.lineage = {key: self.read_pin(pin) for key, pin in m["lineage"].items()}
        self._lineage_links()
        self._wheel_members()
        self.validate_sources()
        self.load_api()
        reader_pin = m["paired_reader_source"]
        assert _same_pin(reader_pin, self.receipts["paired_reader"]["reader"])
        self.verify(reader_pin)
        assert _path(reader_pin["path"]) == ROOT / "tests/qualification/test_rank_paired_reader.py"
        assert self.receipts["paired_reader"]["actual_outcomes"] == {
            "cases": 26, "failures": 0, "errors": 0, "skipped": 0}
        assert _same_pin(self.receipts["paired_reader"]["capture"], m["receipts"]["paired"])
        self.paired = _load_pure_reader(_path(reader_pin["path"]), reader_pin["sha256"], "_rank_current_paired_reader")
        assert len(self.paired.EXPECTED_COLLECTION_IDS) == 26
        assert set(self.paired.READERS) == set(CURRENT_CASE_IDS)
        runs = self.receipts["paired"]["runs"]
        self.ui_runs = {(run["context"], run["case_id"]): run for run in runs}
        assert len(runs) == len(self.ui_runs) == 26
        assert set(self.ui_runs) == set(itertools.product(CONTEXTS[2:], CURRENT_CASE_IDS))
        assert self.receipts["paired"]["case_ids"] == list(CURRENT_CASE_IDS)
        self._current_launches()
        self._comparison_source_snapshots()

    def _comparison_source_snapshots(self):
        roles = {"reader": "reader_source", "extraction": "extraction_source", "paired": "paired_reader_source"}
        self.comparison_sources = {role: self.manifest[field] for role, field in roles.items()}
        scope = self.manifest.get("scope")
        if not isinstance(scope, dict) or "comparison_source_snapshots" not in scope:
            return  # Preserve the original optional scope contract.
        snapshots = scope["comparison_source_snapshots"]
        assert isinstance(snapshots, dict) and set(snapshots) == set(roles)
        for role, field in roles.items():
            pair = snapshots[role]
            assert isinstance(pair, dict) and set(pair) == {"active", "snapshot"}
            active, snapshot = pair["active"], pair["snapshot"]
            assert isinstance(active, dict) and isinstance(snapshot, dict)
            assert _same_pin(active, self.manifest[field]), "comparison active source identity differs: " + role
            assert shared._pin_size(snapshot) == shared._pin_size(active) and snapshot["sha256"] == active["sha256"], "comparison snapshot bytes differ: " + role
            active_path, snapshot_path = _path(active["path"]), _path(snapshot["path"])
            assert "logical_path" in snapshot and _path(snapshot["logical_path"]) == active_path, "comparison logical source differs: " + role
            self.verify(active)
            self.verify(snapshot)
            assert active_path != snapshot_path and not active_path.samefile(snapshot_path), "comparison snapshot must be a distinct physical file: " + role
            self.comparison_sources[role] = snapshot

    def _contracts(self):
        assert self.adoption["state"] == "adopted_scoped_comparison_mapping_no_option_acceptance"
        assert self.adoption["acceptance"] is False
        assert _same_pin(self.adoption["proposal"], self.manifest["proposal"])
        assert self.adoption["scope"] == {"requirements": 6, "contexts": 4,
                                         "expected_assertions": 196, "complete_requirement_coverage": False}
        adopted = {row["id"]: row for row in self.adoption["contracts"]}
        self.contracts = {}
        for contract in self.proposal["proposed_surface_contracts"]:
            stem = contract["requirement_id"].removeprefix("G042.rank-imagej.")
            assert stem in STEMS and stem not in self.contracts
            assert set(contract["contexts"]) == set(CONTEXTS)
            frozen = {key: contract[key] for key in ("id", "requirement_id", "snapshot", "contexts")}
            assert contract["sha256"] == adopted[contract["id"]]["sha256"] == _sha(_canonical(frozen))
            assert adopted[contract["id"]]["requirement_id"] == contract["requirement_id"]
            for context, assertions in contract["contexts"].items():
                ids = [row["id"] for row in assertions]
                assert ids and len(ids) == len(set(ids))
                if context == "cpp_api":
                    assert set(ids) == set(shared.SDK_IDS[stem])
                elif context == "python_api":
                    assert set(ids) == set(shared.PYTHON_IDS[stem])
            self.contracts[stem] = contract
        assert set(self.contracts) == set(STEMS) and len(adopted) == 6
        assert sum(len(c["contexts"][context]) for c in self.contracts.values() for context in CONTEXTS) == 196

    def _lineage_links(self):
        m, r, lineage = self.manifest, self.receipts, self.lineage
        assert _same_pin(m["lineage"]["sdk_inputs"], r["sdk"]["inputs"])
        assert _same_pin(m["lineage"]["host_inputs"], r["host"]["frozen"])
        assert _same_pin(m["lineage"]["installation"], r["python"]["installation"])
        assert _same_pin(m["lineage"]["installation"], r["paired"]["installation"])
        assert _same_pin(m["lineage"]["wheel_build"], lineage["installation"]["wheel_build"])
        assert _same_pin(m["lineage"]["wheel_inputs"], lineage["wheel_build"]["inputs"])
        assert _same_pin(lineage["installation"]["wheel"], lineage["wheel_build"]["wheel"])
        assert r["sdk"]["state"] == "fresh_matching_ABI_installed_core_only_SDK31_473checks_actual55ID82rows_passed"
        assert r["host"]["state"] == "all34_command_and36_host_cases_actual_resource_and_provider_values_passed"
        assert r["python"]["actual_outcomes"] == {"cases": 47, "failures": 0, "errors": 0, "skipped": 0}
        assert r["paired"]["state"] == "all26_raw_installed_paired_observations_emitted_reader_pending"
        assert len(r["sdk"]["installed_products"]) == 51
        assert r["sdk"]["paired_headers"] == 28

    def _wheel_members(self):
        """Verify runtime bytes at immutable wheel members, preserving original paths."""
        installation = self.lineage["installation"]
        self.wheel_pin = installation["wheel"]
        self.verify(self.wheel_pin, artifact=True)
        package = installation["wheel_equal_package_members"]
        prefix = installation["prefix_members"]
        assert isinstance(package, list) and len(package) == 37
        assert isinstance(prefix, list) and 37 <= len(prefix) <= 128
        package_init, = [pin for pin in package if _path(pin["path"]).name == "__init__.py"
                         and _path(pin["path"]).parent.name == "imagejxx"]
        self.package_root = _path(package_init["path"]).parent
        executable, = [pin for pin in prefix if _path(pin["path"]).name == "imagejxx.exe"]
        self.installed_executable = executable
        prefix_root = _path(executable["path"]).parent.parent
        self.member_pins = {_path(pin["path"]): pin for pin in package + prefix}
        assert len(self.member_pins) == len(package) + len(prefix)
        self.native_pin, = [pin for pin in package if _path(pin["path"]).name.startswith("_native.")]
        self.desktop_pin, = [pin for pin in package if _path(pin["path"]).name.startswith("_desktop.")]
        assert {_pin_identity(pin) for pin in self.receipts["python"]["installed_members"]} == {
            _pin_identity(pin) for pin in package}
        assert {_pin_identity(pin) for pin in self.receipts["paired"]["installed_members"]} == {
            _pin_identity(pin) for pin in package + prefix}
        with zipfile.ZipFile(_path(self.wheel_pin["path"])) as wheel:
            infos = wheel.infolist()
            names = {info.filename: info for info in infos}
            assert 0 < len(infos) == len(names) <= 256
            assert sum(info.file_size for info in infos) <= shared.MAX_FILE
            for pin in package + prefix:
                path = _path(pin["path"])
                member = (("imagejxx/" + path.relative_to(self.package_root).as_posix())
                          if path.is_relative_to(self.package_root) else path.relative_to(prefix_root).as_posix())
                info = names[member]
                assert not info.is_dir() and not info.flag_bits & 1 and info.file_size == shared._pin_size(pin)
                digest, count, crc = hashlib.sha256(), 0, 0
                with wheel.open(info) as source:
                    for chunk in iter(lambda: source.read(1024 * 1024), b""):
                        count += len(chunk)
                        assert count <= info.file_size
                        digest.update(chunk)
                        crc = zlib.crc32(chunk, crc)
                assert count == info.file_size and digest.hexdigest() == pin["sha256"]
                assert crc & 0xffffffff == info.CRC
                self.archived[_pin_identity(pin)] = {"archive": self.wheel_pin,
                    "member": member, "original": pin, "member_map": self.manifest["lineage"]["installation"]}

    def validate_sources(self):
        m, lineage = self.manifest, self.lineage
        authorities = {
            "sdk": lineage["sdk_inputs"]["inputs"],
            "host": lineage["host_inputs"]["inputs"],
            "python": self.receipts["python"]["sources"] + lineage["wheel_inputs"]["inputs"],
            "imgui_cpp": lineage["wheel_inputs"]["inputs"],
            "imgui_python": lineage["wheel_inputs"]["inputs"],
        }
        required = {
            "sdk": {"examples/cpp/rank_command.cpp", "examples/cpp/rank_observations.hpp", "examples/cpp/CMakeLists.txt"},
            "host": {"tests/native/test_rank_host.cpp", "examples/cpp/rank_observations.hpp", "src/desktop/window.cpp"},
            "python": {"tests/python/test_rank_editor_host.py", "tests/python/test_rank_editor_ui.py",
                       "tests/python/rank_observations.py", "tests/qualification/test_rank_api_observations.py"},
            "imgui_cpp": {"src/desktop/rank_ui_probe.cpp", "src/desktop/rank_panel.cpp", "src/desktop/window.cpp", "src/desktop/main.cpp"},
            "imgui_python": {"src/desktop/rank_ui_probe.cpp", "src/desktop/rank_panel.cpp", "src/desktop/window.cpp", "src/desktop/python.cpp"},
        }
        for key, producer in m["producers"].items():
            assert producer["context"] == ("cpp_api" if key in ("sdk", "host") else "python_api" if key == "python" else key)
            assert producer["id"] == (shared.RAW_PRODUCERS[key] if key in shared.RAW_PRODUCERS
                                       else "p06f.rank-ui.001." + key + ".wheel003")
            assert isinstance(producer["sources"], list) and 0 < len(producer["sources"]) <= 512
            assert isinstance(producer["artifacts"], list) and 0 < len(producer["artifacts"]) <= 128
            authority = {}
            for pin in authorities[key]:
                authority.setdefault(_path(pin["path"]), []).append(pin)
            consumed = set()
            for pin in producer["sources"]:
                logical = _path(pin.get("logical_path", pin["path"]))
                assert logical in authority, "named consumed-source lineage gap: " + str(logical)
                assert any(shared._pin_size(pin) == shared._pin_size(old) and pin["sha256"] == old["sha256"]
                           for old in authority[logical]), "source snapshot differs from executed source: " + str(logical)
                self.verify(pin)
                consumed.add(logical)
            assert {_path(path) for path in required[key]} <= consumed, "required consumed source lineage is missing: " + key
            for pin in producer["artifacts"]:
                self.verify(pin, artifact=True)
            artifacts = {_pin_identity(pin) for pin in producer["artifacts"]}
            if key == "sdk":
                assert _pin_identity(self.receipts["sdk"]["consumer_executable"]) in artifacts
                installed = self.receipts["sdk"]["installed_products"]
                for pin in installed:
                    assert _pin_identity(pin) in artifacts, "actual installed SDK header/library/member absent"
                core = lineage["sdk_inputs"]["core"]
                assert any(Path(pin["path"]).name == "imagejxx_core.lib" and pin["sha256"] == core["sha256"]
                           and shared._pin_size(pin) == shared._pin_size(core) for pin in installed)
            elif key == "host":
                host_exe, = [pin for pin in self.receipts["host"]["products"] if Path(pin["path"]).name == "imagejxx_rank_host_tests.exe"]
                assert _pin_identity(host_exe) in artifacts
            else:
                assert _pin_identity(self.wheel_pin) in artifacts
                assert not any(Path(pin["path"]).name == "imagejxx_core.lib" for pin in producer["artifacts"]), "static SDK library is not a Python/UI loaded artifact"

    def load_api(self):
        m, r = self.manifest, self.receipts
        assert _same_pin(m["raw"]["sdk"], r["sdk"]["raw_observations"])
        assert _same_pin(m["raw"]["host"], r["host"]["host_raw"])
        sdk = self.add_raw("sdk", m["raw"]["sdk"])
        host = self.add_raw("host", m["raw"]["host"])
        sdk_adopted = {oid for ids in shared.SDK_IDS.values() for oid in ids if oid.startswith("sdk.")}
        assert len(sdk) == 31 and set(self.raw["sdk"]) == sdk_adopted | SDK_EXTRA_IDS
        assert len(self.raw["sdk"]) == 55 and sum(len(case["observations"]) for case in sdk) == 82
        assert sum(not case["observations"] for case in sdk) == 5
        assert len(host) == 36 and set(self.raw["host"]) == HOST_ADOPTED_IDS | HOST_EXTRA_IDS
        assert sum(not case["observations"] for case in host) == 32
        python = r["python"]
        assert len(python["actual_cases"]) == 47 and python["observation_count"] == 957
        for case_id, descriptor in python["actual_cases"].items():
            assert descriptor["observation_count"] == len(descriptor["observation_ids"])
            self.add_raw("python", descriptor["raw"], only_case=case_id, observation_ids=descriptor["observation_ids"])
        python_adopted = {oid for ids in shared.PYTHON_IDS.values() for oid in ids}
        assert len(python_adopted) == 39 and python_adopted <= set(self.raw["python"])
        origins = self.raw["python"]["python.origins"]
        assert len(origins) == 47
        for row in origins:
            observation = row["observation"]
            assert observation["encoding"] == "canonical_json"
            paths = observation["value"]
            assert {"imagejxx", "imagejxx._native", "imagejxx._desktop"} <= paths.keys()
            for module, origin in paths.items():
                assert module == "imagejxx" or module.startswith("imagejxx.")
                path = _path(origin)
                assert path.is_relative_to(self.package_root) and path in self.member_pins
            assert _path(paths["imagejxx._native"]) == _path(self.native_pin["path"])
            assert _path(paths["imagejxx._desktop"]) == _path(self.desktop_pin["path"])

    def _current_launches(self):
        for (context, case), run in self.ui_runs.items():
            assert run["step"]["exit_code"] == 0 and run["state"] == "raw_observations_emitted"
            argv = run["step"]["argv"]
            assert isinstance(argv, list) and argv
            if context == "imgui_cpp":
                assert _path(argv[0]) == _path(self.installed_executable["path"])
                assert argv[1:3] == ["--rank-ui-case", case]
            else:
                assert _path(argv[0]) == _path(self.receipts["python"]["python"]["path"])
                assert "_rank_ui_case" in " ".join(argv)

    def python_lifecycle_support(self, oid, details):
        if oid == "python.callback.reserved_bytes":
            return super().python_lifecycle_support(oid, details)
        assert oid == "python.gil.thread_progress"
        case_id = "test_rao03_rank_releases_gil_for_cross_thread_cancel_after_initial_progress"

        def actual(name):
            rows = [row for row in self.raw["python"][name] if row["source_case"] == case_id]
            assert len(rows) == 1
            return rows[0]["observation"]["value"]

        assert {row["source_case"] for row in self.raw["python"][oid]} == {case_id}
        profile = {name.removeprefix("runtime.gil_profile."): actual(name)
                   for name in self.raw["python"] if name.startswith("runtime.gil_profile.")}
        assert profile["implementation"] == "cpython" and type(profile["runtime_check_available"]) is bool
        if profile["runtime_check_available"]:
            assert profile["runtime_enabled"] is True
        else:
            assert profile["runtime_enabled"] is None
            assert profile["version_major"] == 3 and profile["version_minor"] == 12
            assert profile["cache_tag"] == "cpython-312" and profile["py_gil_disabled"] in (None, 0)
            assert profile["extension_suffix"] == ".cp312-win_amd64.pyd"
        assert self.native_pin["path"].endswith(profile["extension_suffix"])
        handoffs = actual("gil.actual_handoffs")
        assert len(handoffs) == 1 and handoffs[0]["callback_active"] is False and handoffs[0]["native_returned"] is False
        shared._positive(handoffs[0]["thread_id"], 2**63 - 1)
        assert actual("rank.progress_calls") == [[0, 1]]
        details["supporting_actuals"] = {"runtime.gil_profile": profile, "gil.actual_handoffs": handoffs,
                                         "rank.progress_calls": actual("rank.progress_calls")}
        details["scope"] = "enabled-GIL handoff after initial progress; positive-work/kernel-entry cancellation is not proved"

    def emit(self, stem, context, assertions, origins, inputs, unsupported):
        m = self.manifest
        keys = ("sdk", "host") if context == "cpp_api" else ("python",) if context == "python_api" else (context,)
        dependencies = [m["producers"][key] for key in keys]
        sources = [self.comparison_sources["reader"], self.comparison_sources["extraction"]]
        if context.startswith("imgui_"):
            sources.append(self.comparison_sources["paired"])
        original_artifacts = []
        for producer in dependencies:
            sources.extend(producer["sources"])
            original_artifacts.extend(producer["artifacts"])
        retained, artifacts = [], []
        for pin in _unique_pins(original_artifacts):
            member = self.archived.get(_pin_identity(pin))
            if member:
                retained.append(member)
                artifacts.extend((member["archive"], member["member_map"]))
            else:
                artifacts.append(pin)
        if context.startswith("imgui_"):
            artifacts.extend((m["receipts"]["paired"], m["receipts"]["paired_reader"]))
        else:
            artifacts.extend(m["receipts"][key] for key in keys)
        sources, artifacts = _unique_pins(sources), _unique_pins(artifacts)
        producer_id = "p06f.rank.current.surface." + context + ".001"
        case_id = "case.rank.current." + stem + "." + context
        report = {"suite": "p06f_rank_current_surfaces", "classname": "tests.qualification.test_rank_current_surface_reader",
                  "name": "test_rank_current_surface[" + stem + "-" + context + "]"}
        contract = self.contracts[stem]
        case = {"id": case_id, "requirement_id": contract["requirement_id"], "context": context,
                "contract_id": contract["id"], "contract_sha256": contract["sha256"], "report_role": "python",
                "report_case_identity": report, "assertions": assertions}
        if context.startswith("imgui_"):
            case["inputs"] = [{key: value for key, value in event.items() if key != "provenance"} for event in inputs]
        limits = list(shared.SCOPES) + [
            "Current SDK004, host36, installed Python47 and paired26 identities are separate from historical F04.",
            "SDK12 reservation IDs, host8 virtual-provider IDs, positive-success RAO05 and paired R13 remain outside the frozen196.",
            "The 31 SDK internal cases are not 31 CTest consumers; this current JUnit report qualifies the typed comparisons."]
        trace = {"schema_version": 1, "run_id": m["run_id"], "producer_id": producer_id,
                 "acceptance_claim": False, "complete_requirement_coverage": False, "cases": [case],
                 "raw_producers": dependencies, "observation_origins": origins,
                 "input_provenance": [{"input_index": index, **event["provenance"]} for index, event in enumerate(inputs)],
                 "input_manifest": self.manifest_pin, "retained_products": retained,
                 "unsupported_inputs": unsupported, "scope_limits": limits,
                 "additional_observation_scope": {"assertion_credit": False, "sdk_reservation_unique_ids": 12,
                     "host_virtual_provider_unique_ids": 8, "paired_extra_case_ids": ["R13"],
                     "receipts": m["receipts"]}}
        pin = self.write_fresh(self.output / (stem + "-" + context + ".trace.json"), trace)
        catalogue = {"id": "trace.rank.current." + stem + "." + context, "path": pin["path"],
                     "sha256": pin["sha256"], "trace_case_id": case_id, "report_role": "python",
                     "report_case_identity": report,
                     "producer": {"id": producer_id, "context": context, "sources": sources, "artifacts": artifacts}}
        self.write_fresh(self.output / (stem + "-" + context + ".catalogue.json"),
                         {"schema_version": 1, "run_id": m["run_id"], "acceptance_claim": False,
                          "complete_requirement_coverage": False, "trace": catalogue,
                          "raw_producers": dependencies, "retained_products": retained})


@pytest.fixture(scope="module")
def current_rank_evidence():
    inputs = os.environ.get("IMAGEJXX_RANK_CURRENT_SURFACE_INPUTS")
    output = os.environ.get("IMAGEJXX_RANK_CURRENT_SURFACE_OUTPUT_DIR")
    assert inputs and output, "both current rank qualification environment paths are required"
    evidence = CurrentRankEvidence(_path(inputs), _path(output))
    yield evidence
    evidence.recheck()
    _no_product_imports()


@pytest.mark.parametrize("stem,context", PARAMETERS, ids=PARAMETER_IDS)
def test_rank_current_surface(stem, context, current_rank_evidence):
    # Identical adopted assertions and physical-input extraction; only the
    # deliberately current carrier admission and trace identities differ.
    shared.test_rank_surface(stem, context, current_rank_evidence)
